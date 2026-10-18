#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class AActor;
class UWorld;
struct FRTSFootprint;

/**
 * The scene rts.Debug.Bench measures in (#132, #138, #172): where its grid stands, and the solids of its obstacle variant.
 *
 * A unit set named with the suffix "@Solids" (ue.ps1 -Unit DA_TestM20Guardian@Solids) benches among solids the navmesh
 * leaves out (#168): tree trunks in a copse, boulders and low walls, spawned when each count's grid stands and cleared with
 * its units. They stand over the ground the grid covers and drives over, at a fixed seed, so every run sees the same scene.
 * Trunks and walls along the drive fit the gaps of the grid and are passed; walls across it and solids where the units'
 * formation stops are met. Without the suffix the scene is L_Bench's open ground, as before.
 */
namespace RTSBenchScene
{
	/**
	 * The grid: units face +X in rows across Y, as a formation would stand: each unit's length plus RowGap apart along X and
	 * its width plus ColumnGap apart along Y, about square overall.
	 */
	constexpr float RowGap = 300.f;
	constexpr float ColumnGap = 250.f;

	/** The short method's grid and move: the middle of the map, then MoveDistance along +X. */
	const FVector GridCentre(0.f, 0.f, 0.f);
	constexpr float MoveDistance = 6000.f;

	/**
	 * The long method's grid and drive: its units shoot at whatever comes into range while they stand, and over minutes 240
	 * of them would destroy the enemy's headquarters and end the match. So the grid stands south-west of the middle, every
	 * unit out of range of the headquarters in the north-east corner, and drives the other way: half as far, to the west.
	 */
	const FVector LongGridCentre(-3000.f, -1000.f, 0.f);
	constexpr float LongMoveDistance = -3000.f;

	/** The set name's suffix that benches among solids. */
	const TCHAR* const SolidsSuffix = TEXT("@Solids");

	/** The set name's suffix that benches with rigid tracks (#151). */
	const TCHAR* const RigidTracksSuffix = TEXT("@RigidTracks");

	/**
	 * The set name's suffix that animates every track (#212): rts.Vehicle.TrackAnimationDistance 0 while it runs, so the
	 * renderer never leaves the links' shift out and every moving track sends its phase, however far from the camera.
	 */
	const TCHAR* const AllTracksSuffix = TEXT("@AllTracks");

	/**
	 * The set name's suffix that spreads its units over MixedTeams teams in turn (#150), so units of one kind carry
	 * different team colours. Their weapons are idled, since neighbours of other teams would otherwise fight.
	 */
	const TCHAR* const MixedTeamsSuffix = TEXT("@MixedTeams");
	constexpr int32 MixedTeams = 4;

	/** The set name's suffix that draws every mesh at LOD0 (r.ForceLOD 0 while it runs; #149), against the LODs it has. */
	const TCHAR* const ForceLOD0Suffix = TEXT("@LOD0");

	/**
	 * The set name's suffixes that put the bench camera nearer or farther than its usual 170 m (#149): about as near as the
	 * RTS camera starts tilting, where the M20 draws LOD1, and as far as its farthest zoom, where it draws LOD3.
	 */
	const TCHAR* const NearSuffix = TEXT("@Near");
	const TCHAR* const FarSuffix = TEXT("@Far");
	constexpr float NearCameraDistance = 6000.f;
	constexpr float FarCameraDistance = 45000.f;

	/**
	 * The set name's suffixes that change the occlusion queries of the units' parts (#214). Without them the parts take
	 * ARTSUnit's grouped occlusion (rts.Render.GroupPartOcclusion). "@ExactOcclusion" gives each part its own query in every
	 * frame, as before #214; "@NoOcclusion" turns hardware occlusion off (r.AllowOcclusionQueries 0) and "@HZB" tests the
	 * hierarchical Z-buffer instead (r.HZBOcclusion 1), both for the whole scene.
	 */
	const TCHAR* const ExactOcclusionSuffix = TEXT("@ExactOcclusion");
	const TCHAR* const NoOcclusionSuffix = TEXT("@NoOcclusion");
	const TCHAR* const HZBOcclusionSuffix = TEXT("@HZB");

	/**
	 * The set name's suffix that roofs most of the ground the grid stands on and drives over (#214): a slab
	 * CoverHeight above the ground over the grid's southern CoverShare across Y and the whole drive, so the units under it
	 * are hidden from the camera and only occlusion culling leaves them out. It neither collides nor changes navigation.
	 */
	const TCHAR* const OccludedSuffix = TEXT("@Occluded");
	constexpr float CoverShare = 2.f / 3.f;
	constexpr float CoverHeight = 1200.f;
	constexpr float CoverThickness = 100.f;
	constexpr float CoverMargin = 1000.f;

	/** The roof of an "@Occluded" set over Hulls and where Drive along X takes them, on ground at GroundZ. */
	WORLDDIVIDE_API FBox MakeCover(TConstArrayView<FBox2D> Hulls, float Drive, float GroundZ);

	/** Spawns Cover into World: a box that blocks nothing. */
	WORLDDIVIDE_API AActor* SpawnCover(UWorld& World, const FBox& Cover);

	/**
	 * The set name's suffix that puts the bench camera at the RTS camera's close zoom (#214): CloseCameraDistance from the
	 * grid's middle, at the pitch ARTSCameraPawn tilts to for that arm, flatter than the top-down -55.
	 */
	const TCHAR* const CloseSuffix = TEXT("@Close");
	constexpr float CloseCameraDistance = 3000.f;

	/** The level the viewpoints below belong to; ue.ps1 -Task bench -Map L_TestMap plays it instead of L_Bench. */
	const TCHAR* const TestMapName = TEXT("L_TestMap");

	/**
	 * A fixed view of L_TestMap (#214), named by its set suffix. The grid stands with the side Align points to on Anchor
	 * (-1 to 1 per axis, 0 centres it there), or, with bBehindHeadquarters, with that side 3 m from team 0's headquarters,
	 * and the camera looks at its middle along Yaw. The map's own units are cleared first, so only the grid is measured.
	 */
	struct FViewpoint
	{
		const TCHAR* Suffix = nullptr;
		const TCHAR* Description = nullptr;
		FVector2D Anchor = FVector2D::ZeroVector;
		FVector2D Align = FVector2D::ZeroVector;
		float Yaw = 0.f;
		bool bBehindHeadquarters = false;
	};

	/** How far the grid of a viewpoint with bBehindHeadquarters keeps from the headquarters. */
	constexpr float HeadquartersGap = 300.f;

	WORLDDIVIDE_API TConstArrayView<FViewpoint> GetTestMapViewpoints();

	/** The viewpoint whose suffix SetName carries, or null. */
	WORLDDIVIDE_API const FViewpoint* FindViewpoint(const FString& SetName);

	/** The middle of a grid whose hulls cover Size, with its Align side on Anchor. */
	WORLDDIVIDE_API FVector2D PlaceGrid(const FVector2D& Anchor, const FVector2D& Align, const FVector2D& Size);

	/** Bumped whenever the solids' layout rules change; recorded in bench.json, so runs of another layout never compare. */
	constexpr int32 SolidsVersion = 1;
	constexpr int32 SolidsSeed = 172;

	/** The box round the hull of Footprint, seen from above: the hull itself while it faces along X or Y. */
	WORLDDIVIDE_API FBox2D GetHullBox(const FRTSFootprint& Footprint);

	/** The places of Count units on a grid of Cell-sized cells round Centre, in the order the bench spawns them. */
	WORLDDIVIDE_API TArray<FVector> MakeGrid(int32 Count, const FVector2D& Cell, const FVector& Centre, int32& OutRows, int32& OutColumns);

	enum class ESolidKind : uint8
	{
		/** An upright cylinder: Size.X is its diameter. */
		Trunk,
		Boulder,
		Wall,
	};

	/** One solid, an axis-aligned shape standing on the ground. */
	struct FSolid
	{
		ESolidKind Kind = ESolidKind::Boulder;
		/** The middle of its foot; Z is left to the ground it is spawned on. */
		FVector Center = FVector::ZeroVector;
		FVector Size = FVector::ZeroVector;

		FBox2D GetBox() const
		{
			return FBox2D(FVector2D(Center) - FVector2D(Size) / 2.f, FVector2D(Center) + FVector2D(Size) / 2.f);
		}

		bool operator==(const FSolid& Other) const { return Kind == Other.Kind && Center == Other.Center && Size == Other.Size; }
	};

	/** How many solids of each kind stand per square metre of the area (trunks: of the copse), and their sizes in cm. */
	namespace Solids
	{
		/** The copse: the area's western share along X and its northern share along Y. */
		constexpr float CopseWest = 0.4f;
		constexpr float CopseNorth = 0.6f;
		constexpr float TrunksPerM2 = 1.f / 200.f;
		constexpr float BouldersPerM2 = 1.f / 1200.f;
		constexpr float WallsPerM2 = 1.f / 4000.f;
		const FVector2D TrunkDiameter(50.f, 80.f);
		constexpr float TrunkHeight = 800.f;
		const FVector2D BoulderSide(120.f, 250.f);
		const FVector2D BoulderHeight(100.f, 150.f);
		const FVector2D WallLength(600.f, 1200.f);
		constexpr float WallThickness = 40.f;
		constexpr float WallHeight = 120.f;
		/** How far every solid keeps from a hull where it spawns, and from any other solid. */
		constexpr float KeepClear = 50.f;
		constexpr float Spacing = 300.f;
		/** How far past the hulls and their drive the area reaches. */
		constexpr float AreaMargin = 1000.f;
		/** Placements tried per solid before a kind stops short of its count. */
		constexpr int32 Attempts = 40;
	}

	/** The ground the solids stand on: round the hulls where they spawn and where Drive along X takes them, plus the margin. */
	WORLDDIVIDE_API FBox2D GetSolidsArea(TConstArrayView<FBox2D> Hulls, float Drive);

	/**
	 * The solids over Area, at SolidsSeed, none within Solids::KeepClear of the hulls in Hulls (the grid where it spawns):
	 * the same for the same Area and Hulls. Area is the ground the units stand and drive on.
	 */
	WORLDDIVIDE_API TArray<FSolid> MakeSolids(const FBox2D& Area, TConstArrayView<FBox2D> Hulls);

	/** Spawns Layout into World on ground at GroundZ: movable static meshes that block pawns and leave navigation alone. */
	WORLDDIVIDE_API TArray<TWeakObjectPtr<AActor>> SpawnSolids(UWorld& World, TConstArrayView<FSolid> Layout, float GroundZ);
}

#endif
