#include "RainPaintGame.h"
#include "PaintHUD.h"

#include "RainShieldTypes.h"
#include "DrawDebugHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraDataSetAccessor.h"
#include "Particles/ParticlePerfStatsManager.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "ProceduralMeshComponent.h"

static TAutoConsoleVariable<int32> CVarPaintRainCount(
	TEXT("rain.paint.Count"),
	100000,
	TEXT("Drops in the paint rain's pool (Paint Valley, System 1), spawned once per level. The paint each drop carries is the level's budget over the pool, so the count changes the cost, not the difficulty."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarPaintCloudWait(
	TEXT("rain.paint.CloudWait"),
	8.0f,
	TEXT("Longest wait (s) of a recycled drop in the cloud before it falls again: with the pool, it sets how many drops are in the air at once."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaintBallCount(
	TEXT("rain.paint.Balls"),
	3500,
	TEXT("Paint balls in System 2's pool (CPU meshes), spawned once per level."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaintBallsGPU(
	TEXT("rain.paint.BallsGPU"),
	0,
	TEXT("0 simulates System 2's paint balls on the CPU (NS_PaintBalls), 1 on the GPU (NS_PaintBallsGPU, the same HLSL): the CPU -> GPU port. Rebuilds the pool."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarPaintBallWait(
	TEXT("rain.paint.BallWait"),
	30.0f,
	TEXT("Longest wait (s) of a paint ball in the cloud before it is launched again."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarPaintBallBudget(
	TEXT("rain.paint.BallBudget"),
	1.5f,
	TEXT("Paint in System 2's whole pool, as a multiple of what the coverage goal needs (each ball carries this share)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaintPauseIdle(
	TEXT("rain.paint.PauseIdle"),
	1,
	TEXT("1: the system not played is paused and hidden; 0: both simulate (the other one idle, for measuring what pausing saves)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarPaintDropOpacity(
	TEXT("rain.paint.DropOpacity"),
	1.0f,
	TEXT("How opaque the paint drops in the air are, times their own opacity (0..2; the panel's slider sets it)."),
	ECVF_Default);

// The pool's tuning, live (the actor's PaintBudget, BigSeedDrops and BigShare follow them).
static TAutoConsoleVariable<float> CVarPaintBudget(
	TEXT("rain.paint.Budget"),
	2.0f,
	TEXT("Paint in the whole pool of small drops, as a multiple of what the coverage goal needs (each small drop carries this share of it)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarPaintBigSeed(
	TEXT("rain.paint.BigSeed"),
	60.0f,
	TEXT("Paint a big drop seeds a puddle with, in small drops' worth."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarPaintBigShare(
	TEXT("rain.paint.BigShare"),
	0.03f,
	TEXT("Share of the pool that are big drops (they seed puddles and are never used up). Changing it refills the pool."),
	ECVF_Default);

namespace
{
	// The pool slider's ranges (a log scale: the cost grows with the count).
	constexpr float RainSliderMin = 10000.0f, RainSliderMax = 400000.0f, BallSliderMin = 100.0f, BallSliderMax = 5000.0f;

	float CountToSlider(int32 Count, bool bBalls)
	{
		const float Lo = bBalls ? BallSliderMin : RainSliderMin, Hi = bBalls ? BallSliderMax : RainSliderMax;
		return FMath::Clamp(FMath::Loge(FMath::Max(float(Count), 1.0f) / Lo) / FMath::Loge(Hi / Lo), 0.0f, 1.0f);
	}

	int32 SliderToCount(float Value, bool bBalls)
	{
		const float Lo = bBalls ? BallSliderMin : RainSliderMin, Hi = bBalls ? BallSliderMax : RainSliderMax;
		const float Step = bBalls ? 10.0f : 1000.0f;
		return FMath::RoundToInt(Lo * FMath::Pow(Hi / Lo, FMath::Clamp(Value, 0.0f, 1.0f)) / Step) * int32(Step);
	}

	/** Box kinds the rain reads back (the splat's size field): blockers, the target, the ground. */
	constexpr float KindGround = 3.0f;

	FColor Hex(const TCHAR* Text)
	{
		return FColor::FromHex(Text);
	}

	FPaintPalette Palette(const TCHAR* Name, const TCHAR* Top, const TCHAR* Light, const TCHAR* Dark, const TCHAR* Bottom, const TCHAR* AO,
		const TCHAR* Opening, const TCHAR* Edge, float EdgeAmount)
	{
		FPaintPalette P;
		P.Name = Name;
		P.Top = Hex(Top);
		P.Light = Hex(Light);
		P.Dark = Hex(Dark);
		P.Bottom = Hex(Bottom);
		P.AO = Hex(AO);
		P.Opening = Hex(Opening);
		P.Edge = Hex(Edge);
		P.EdgeAmount = EdgeAmount;
		return P;
	}

	// The levels. Palettes sampled from the references (Docs/ArtDirection.md).
	TArray<FPaintLevel> DefaultLevels()
	{
		TArray<FPaintLevel> Levels;

		// ---- 1: the peach keep (after mv_2_09): a chunky pink floating base with a skirt and a crenellated rim; a
		// cream keep with a vaulted passage, a slender tower with a balcony, a corbelled crown, a teardrop spire and a
		// pennant; a stair of shallow steps; a bridge from the balcony to a small tower and on to a second, higher island;
		// a teal gate.
		// Built first for its palettes; listed second (a showcase: the game itself uses blocks only).
		FPaintLevel Keep;
		FPaintLevel& L = Keep;
		L.Name = TEXT("Peach Keep");
		enum { Cream, Pink, Teal, Navy };
		FPaintPalette CreamP = Palette(TEXT("Cream"), TEXT("e2edd2"), TEXT("cad2bb"), TEXT("b3b488"), TEXT("8f8a64"), TEXT("e2dace"), TEXT("5a4636"), TEXT("ecf5de"), 0.5f);
		CreamP.OpeningBarrel = Hex(TEXT("e2edd2"));
		CreamP.OpeningShift = -1.6f;
		CreamP.Grad = Hex(TEXT("e4e0a0"));
		CreamP.GradAmount = 0.7f;
		FPaintPalette PinkP = Palette(TEXT("Pink"), TEXT("ffc9cf"), TEXT("fd6d90"), TEXT("f8544f"), TEXT("c43a50"), TEXT("f6c4c8"), TEXT("980056"), TEXT("ffd6dc"), 0.5f);
		PinkP.OpeningRim = Hex(TEXT("ffb4c8"));
		PinkP.OpeningRimWidth = 4.2f;
		PinkP.OpeningBarrel = Hex(TEXT("b0558a"));
		PinkP.OpeningShift = 3.5f;
		PinkP.TileSize = 30.0f;
		PinkP.TileLine = 1.0f;
		PinkP.TileDarken = 0.06f;
		L.Palettes = {
			CreamP,
			PinkP,
			Palette(TEXT("Teal"), TEXT("4fcec1"), TEXT("2fb5c3"), TEXT("22a3ae"), TEXT("0a6e72"), TEXT("c4dcdc"), TEXT("0a5a60"), TEXT("62d8cb"), 0.5f),
			Palette(TEXT("Navy"), TEXT("3a5068"), TEXT("5a6e80"), TEXT("1b2a3c"), TEXT("101c2a"), TEXT("ffffff"), TEXT("000000"), TEXT("ffffff"), 0.0f),
		};
		auto Box = [&L](double X0, double Y0, double Z0, double X1, double Y1, double Z1, EPaintBoxKind Kind, int32 Pal, bool bCollide = true)
		{
			FPaintBox& B = L.Boxes.AddDefaulted_GetRef();
			B.Min = FVector(X0, Y0, Z0);
			B.Max = FVector(X1, Y1, Z1);
			B.Kind = Kind;
			B.Palette = Pal;
			B.bCollide = bCollide;
		};
		// Merlons along a wall top: Count of them from A to B (corners included), Size along, Height tall, Thick deep.
		auto Merlons = [&Box](const FVector& A, const FVector& B, int32 Count, double Size, double Height, double Thick, EPaintBoxKind Kind, int32 Pal, int32 Skip0, int32 Skip1)
		{
			const FVector Dir = (B - A).GetSafeNormal();
			const double Len = (B - A).Size();
			const double Step = (Len - Size) / (Count - 1);
			const FVector Across = FVector(-Dir.Y, Dir.X, 0.0).GetAbs();
			for (int32 k = Skip0; k < Count - Skip1; ++k)
			{
				const FVector P0 = A + Dir * (Step * k);
				const FVector P1 = P0 + Dir * Size + Across * Thick + FVector(0, 0, Height);
				Box(FMath::Min(P0.X, P1.X), FMath::Min(P0.Y, P1.Y), P0.Z, FMath::Max(P0.X, P1.X), FMath::Max(P0.Y, P1.Y), P1.Z, Kind, Pal, false);
			}
		};
		// A crenellated parapet around a rectangle's top (walls Thick deep, WallH tall, then merlons; Open: a side left
		// open, 0..3 = -Y, +Y, -X, +X, or -1).
		auto Rim = [&Box, &Merlons](double X0, double Y0, double X1, double Y1, double Z, double Thick, double WallH, int32 Count, double MSize, double MHeight,
			EPaintBoxKind Kind, int32 Pal, int32 Open = -1)
		{
			const double T = Z + WallH;
			if (Open != 0) { Box(X0, Y0, Z, X1, Y0 + Thick, T, Kind, Pal, false); Merlons(FVector(X0, Y0, T), FVector(X1, Y0, T), Count, MSize, MHeight, Thick, Kind, Pal, 0, 0); }
			if (Open != 1) { Box(X0, Y1 - Thick, Z, X1, Y1, T, Kind, Pal, false); Merlons(FVector(X0, Y1 - Thick, T), FVector(X1, Y1 - Thick, T), Count, MSize, MHeight, Thick, Kind, Pal, 0, 0); }
			if (Open != 2) { Box(X0, Y0 + Thick, Z, X0 + Thick, Y1 - Thick, T, Kind, Pal, false); Merlons(FVector(X0, Y0, T), FVector(X0, Y1, T), Count, MSize, MHeight, Thick, Kind, Pal, 1, 1); }
			if (Open != 3) { Box(X1 - Thick, Y0 + Thick, Z, X1, Y1 - Thick, T, Kind, Pal, false); Merlons(FVector(X1 - Thick, Y0, T), FVector(X1 - Thick, Y1, T), Count, MSize, MHeight, Thick, Kind, Pal, 1, 1); }
		};
		auto Opening = [&L](const FVector& At, const FVector& N, float Width, float Height, bool bPointed, bool bRound = false)
		{
			FPaintOpening& O = L.Openings.AddDefaulted_GetRef();
			O.At = At;
			O.Normal = N;
			O.Width = Width;
			O.Height = Height;
			O.bPointed = bPointed;
			O.bRound = bRound;
		};
		auto Arch = [&L](const FVector& Min, const FVector& Max, int32 Axis, float Center, float Width, float Spring, bool bPointed, EPaintBoxKind Kind, int32 Pal)
		{
			FPaintArch& A = L.Arches.AddDefaulted_GetRef();
			A.Min = Min;
			A.Max = Max;
			A.Axis = Axis;
			A.Center = Center;
			A.Width = Width;
			A.Spring = Spring;
			A.bPointed = bPointed;
			A.Kind = Kind;
			A.Palette = Pal;
		};
		auto Spire = [&L](const FVector& Base, float Radius, float Height, float Bulge, int32 Pal)
		{
			FPaintSpire& S = L.Spires.AddDefaulted_GetRef();
			S.Base = Base;
			S.Radius = Radius;
			S.Height = Height;
			S.Sides = 8;
			S.Bulge = Bulge;
			S.Palette = Pal;
		};

		// The floating base: a chunky block with a skirt at the foot and a crenellated rim (7 merlons a side, 1:1).
		Box(-105, -105, -75, 105, 105, 0, EPaintBoxKind::Ground, Pink);
		Box(-111, -111, -75, 111, 111, -64, EPaintBoxKind::Ground, Pink, false);
		Rim(-105, -105, 105, 105, 0, 6, 8, 7, 16, 16, EPaintBoxKind::Ground, Pink);
		for (int32 k = -1; k <= 1; ++k)
		{
			// Portholes on all four sides (the view turns).
			for (const FVector& N : { FVector(0, -1, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(1, 0, 0) })
			{
				const FVector Along(-N.Y, N.X, 0.0);
				Opening(N * 105.0 + Along * (50.0 * k) + FVector(0, 0, -38.0), N, 24.0f, 0.0f, false, true);
			}
		}

		// The keep (to paint): a vaulted passage under it, a door, a stair of 14 shallow steps up its front.
		Arch(FVector(10, 10, 0), FVector(100, 100, 70), 1, 45.0f, 34.0f, 24.0f, false, EPaintBoxKind::Target, Cream);
		Opening(FVector(10, 72, 0), FVector(-1, 0, 0), 18.0f, 24.0f, true);
		for (int32 k = 0; k < 14; ++k)
		{
			Box(10 - 7 * (k + 1), 15, 0, 10 - 7 * k, 40, 70 - 5 * k, EPaintBoxKind::Target, Cream);
		}
		// The tower: an arched door at its foot, a balcony at 40 %, slit windows, a band and a corbelled crown.
		Box(40, 40, 70, 84, 84, 300, EPaintBoxKind::Target, Cream);
		Opening(FVector(62, 40, 70), FVector(0, -1, 0), 22.0f, 28.0f, true);
		Box(32, 32, 150, 92, 92, 158, EPaintBoxKind::Target, Cream);
		Rim(32, 32, 92, 92, 158, 4, 3, 5, 9, 8, EPaintBoxKind::Target, Cream, 2);
		for (const FVector& N : { FVector(0, -1, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(1, 0, 0) })
		{
			const FVector Face = FVector(62, 62, 0) + N * 22.0;
			Opening(Face + FVector(0, 0, 232), N, 12.0f, 26.0f, true);
			Opening(Face + FVector(0, 0, 178), N, 12.0f, 26.0f, true);
		}
		Box(37, 37, 292, 87, 87, 296, EPaintBoxKind::Target, Cream);
		Box(35, 35, 300, 89, 89, 310, EPaintBoxKind::Target, Cream);
		Box(31, 31, 310, 93, 93, 320, EPaintBoxKind::Target, Cream);
		Rim(31, 31, 93, 93, 320, 5, 3, 3, 12, 12, EPaintBoxKind::Target, Cream);
		Spire(FVector(62, 62, 320), 16.0f, 86.0f, 0.35f, Navy);
		Box(61.2, 61.2, 400, 62.8, 62.8, 426, EPaintBoxKind::Ground, Navy, false);
		FPaintRibbon& Flag = L.Ribbons.AddDefaulted_GetRef();
		Flag.Start = FVector(62.8, 61.2, 419);
		Flag.Direction = FVector(1, -1, 0);
		Flag.Length = 105.0f;
		Flag.Height = 13.0f;
		Flag.Waves = 2.0f;
		Flag.Amplitude = 9.0f;
		Flag.Palette = Pink;

		// A bridge from the balcony to a small tower with a crown and a dome, and on to a second, higher island.
		Box(-67, 60, 144, 32, 88, 158, EPaintBoxKind::Target, Cream);
		Box(-95, 60, 0, -67, 88, 160, EPaintBoxKind::Target, Cream);
		Opening(FVector(-81, 60, 0), FVector(0, -1, 0), 14.0f, 18.0f, true);
		Arch(FVector(-95, 60, 160), FVector(-67, 88, 198), 1, 14.0f, 14.0f, 14.0f, false, EPaintBoxKind::Target, Cream);
		Box(-99, 56, 198, -63, 92, 206, EPaintBoxKind::Target, Cream);
		Rim(-99, 56, -63, 92, 206, 4, 2, 3, 10, 9, EPaintBoxKind::Target, Cream);
		Spire(FVector(-81, 74, 206), 12.0f, 38.0f, 0.3f, Navy);
		Box(-185, 60, 144, -95, 88, 158, EPaintBoxKind::Target, Cream);
		Box(-265, 34, 126, -185, 114, 158, EPaintBoxKind::Ground, Pink);
		Box(-269, 30, 126, -181, 118, 131, EPaintBoxKind::Ground, Pink, false);
		Rim(-265, 34, -185, 114, 158, 5, 6, 4, 12, 12, EPaintBoxKind::Ground, Pink, 3);
		Opening(FVector(-238, 34, 134), FVector(0, -1, 0), 16.0f, 0.0f, false, true);
		Opening(FVector(-212, 34, 134), FVector(0, -1, 0), 16.0f, 0.0f, false, true);
		Opening(FVector(-265, 74, 134), FVector(-1, 0, 0), 16.0f, 0.0f, false, true);

		// Blockers: a teal gate (a round arch) and a teal block on the second island.
		Arch(FVector(20, -96, 0), FVector(96, -66, 104), 1, 38.0f, 40.0f, 40.0f, false, EPaintBoxKind::Blocker, Teal);
		Box(-250, 50, 158, -218, 82, 192, EPaintBoxKind::Blocker, Teal);

		L.ViewTarget = FVector(-35, 45, 140);
		L.ViewHeight = 650.0f;
		L.BackCenter = Hex(TEXT("ffe9dc"));
		L.BackEdge = Hex(TEXT("ffe9dc"));
		L.BackGlow = Hex(TEXT("f6b78e"));
		L.BackGlowPos = FVector4(0.06, 1.02, 0.5, 0.9);
		L.ShadowAt = FVector(0, 0, -75);
		L.ShadowHalf = FVector2D(111, 111);
		L.ShadowSize = FVector2D(125.0, 26.0);
		L.ShadowColor = Hex(TEXT("f8c09c"));
		L.ShadowAmount = 0.85f;
		L.GradRange = FVector2D(200.0, 330.0);

		// ---- 1: blocks. The game's levels are made of grid cells only (30 cm): a pink floating base exactly 10 x 10
		// cells, a cream shape to paint, teal blockers, in the same palettes and look.
		FPaintLevel Blocks;
		Blocks.Name = TEXT("Blocks");
		Blocks.Palettes = Keep.Palettes;
		auto Cells = [&Blocks](int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, EPaintBoxKind Kind, int32 Pal)
		{
			// Cells X0..X1 (inclusive) etc., cell (0, 0, 0) on the base's top at the centre.
			FPaintBox& B = Blocks.Boxes.AddDefaulted_GetRef();
			B.Min = FVector(30.0 * X0, 30.0 * Y0, 30.0 * Z0);
			B.Max = FVector(30.0 * (X1 + 1), 30.0 * (Y1 + 1), 30.0 * (Z1 + 1));
			B.Kind = Kind;
			B.Palette = Pal;
		};
		FPaintBox& Base = Blocks.Boxes.AddDefaulted_GetRef();
		Base.Min = FVector(-150, -150, -60);
		Base.Max = FVector(150, 150, 0);
		Base.Kind = EPaintBoxKind::Ground;
		Base.Palette = Pink;
		// The shape: a gate of two pillars and a lintel, with two steps.
		Cells(-2, -1, 0, -1, 0, 3, EPaintBoxKind::Target, Cream);
		Cells(1, -1, 0, 2, 0, 2, EPaintBoxKind::Target, Cream);
		Cells(-2, -1, 4, 2, 0, 4, EPaintBoxKind::Target, Cream);
		Cells(1, -1, 3, 2, 0, 3, EPaintBoxKind::Target, Cream);
		Cells(-2, -3, 0, 2, -2, 0, EPaintBoxKind::Target, Cream);
		Cells(-1, 1, 0, 0, 2, 1, EPaintBoxKind::Target, Cream);
		// Blockers: a plate floating over one pillar, a low wall.
		Cells(0, -2, 7, 3, 1, 7, EPaintBoxKind::Blocker, Teal);
		Cells(-4, -4, 0, -4, 1, 1, EPaintBoxKind::Blocker, Teal);
		Blocks.ViewTarget = FVector(0, 0, 25);
		Blocks.ViewHeight = 520.0f;
		Blocks.BackCenter = Keep.BackCenter;
		Blocks.BackEdge = Keep.BackEdge;
		Blocks.BackGlow = Keep.BackGlow;
		Blocks.BackGlowPos = Keep.BackGlowPos;
		Blocks.ShadowAt = FVector(0, 0, -60);
		Blocks.ShadowHalf = FVector2D(150, 150);
		Blocks.ShadowSize = FVector2D(170.0, 30.0);
		Blocks.ShadowColor = Keep.ShadowColor;
		Blocks.ShadowAmount = Keep.ShadowAmount;
		Blocks.GradRange = FVector2D(60.0, 240.0);
		Blocks.ParSeconds = 45.0f;
		Blocks.Ink = Hex(TEXT("6ab4ec"));
		// Blockers in Monument Valley 2's lilac, apart from the blue paint.
		Blocks.Palettes[Teal] = Palette(TEXT("Lilac"), TEXT("dcc6ec"), TEXT("bd9ad8"), TEXT("9a79bd"), TEXT("775b98"), TEXT("e4dcee"), TEXT("5a4870"), TEXT("ecdcf6"), 0.5f);
		Levels.Add(Blocks);

		// More block levels on the same base and look: two for the rain, two built around the paint balls' bounces.
		auto Level = [&Blocks](const TCHAR* Name, float Par, bool bBalls)
		{
			FPaintLevel L = Blocks;
			L.Name = Name;
			L.ParSeconds = Par;
			L.bPaintBalls = bBalls;
			L.Boxes.SetNum(1);
			return L;
		};
		auto Cell = [](FPaintLevel& L, int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, EPaintBoxKind Kind, int32 Pal)
		{
			FPaintBox& B = L.Boxes.AddDefaulted_GetRef();
			B.Min = FVector(30.0 * X0, 30.0 * Y0, 30.0 * Z0);
			B.Max = FVector(30.0 * (X1 + 1), 30.0 * (Y1 + 1), 30.0 * (Z1 + 1));
			B.Kind = Kind;
			B.Palette = Pal;
		};
		const EPaintBoxKind T = EPaintBoxKind::Target, K = EPaintBoxKind::Blocker;
		// Canopy (rain): a long low block under a wide floating roof, a wall behind it and a pillar off to the side.
		// The roof shades the block's top from steep rain: tilt the rain low to reach under it.
		FPaintLevel Canopy = Level(TEXT("Canopy"), 55.0f, false);
		Cell(Canopy, -2, -1, 0, 2, 1, 1, T, Cream);
		Cell(Canopy, -4, 3, 0, -4, 3, 3, T, Cream);
		Cell(Canopy, -3, -2, 4, 3, 2, 4, K, Teal);
		Cell(Canopy, -4, -4, 0, 4, -4, 2, K, Teal);
		Levels.Add(Canopy);
		// Courtyard (rain): a block with a small tower inside walls, open on one side: the walls shade the lower faces.
		FPaintLevel Court = Level(TEXT("Courtyard"), 55.0f, false);
		Cell(Court, -1, -1, 0, 1, 1, 2, T, Cream);
		Cell(Court, -1, -1, 3, -1, -1, 4, T, Cream);
		Cell(Court, -4, -4, 0, 4, -4, 2, K, Teal);
		Cell(Court, -4, 4, 0, 4, 4, 2, K, Teal);
		Cell(Court, -4, -3, 0, -4, 3, 2, K, Teal);
		Cell(Court, 4, -3, 0, 4, -1, 2, K, Teal);
		Cell(Court, 4, 1, 0, 4, 3, 2, K, Teal);
		Levels.Add(Court);
		// Nook (paint balls): the target sits in a nook roofed and walled on three sides, open towards -X: send the balls
		// skipping over the ground into it.
		FPaintLevel Nook = Level(TEXT("Nook"), 45.0f, true);
		Cell(Nook, 0, -1, 0, 2, 1, 1, T, Cream);
		Cell(Nook, -1, -2, 3, 3, 2, 3, K, Teal);
		Cell(Nook, 3, -2, 0, 3, 2, 2, K, Teal);
		Cell(Nook, -1, -2, 0, 2, -2, 2, K, Teal);
		Cell(Nook, -1, 2, 0, 2, 2, 2, K, Teal);
		Cell(Nook, -4, 2, 0, -4, 4, 0, T, Cream);
		Levels.Add(Nook);
		// Pinball (paint balls): four target pillars of different heights between floating bumpers and a wall.
		FPaintLevel Pin = Level(TEXT("Pinball"), 50.0f, true);
		Cell(Pin, -3, -3, 0, -3, -3, 3, T, Cream);
		Cell(Pin, 2, -2, 0, 2, -2, 1, T, Cream);
		Cell(Pin, -1, 2, 0, -1, 2, 2, T, Cream);
		Cell(Pin, 3, 3, 0, 3, 3, 4, T, Cream);
		Cell(Pin, -1, -1, 3, 1, 1, 3, K, Teal);
		Cell(Pin, -4, 0, 0, -4, 4, 1, K, Teal);
		Cell(Pin, 1, -4, 2, 3, -4, 2, K, Teal);
		Levels.Add(Pin);
		// Stairs (rain): four steps rising to the plate that floats over the top two: straight-down rain misses them.
		FPaintLevel Stairs = Level(TEXT("Stairs"), 45.0f, false);
		for (int32 k = 0; k < 4; ++k)
		{
			Cell(Stairs, -2 + k, -1, k, 1, 1, k, T, Cream);
		}
		Cell(Stairs, 0, -2, 6, 2, 2, 6, K, Teal);
		Levels.Add(Stairs);
		// Table (paint balls, a bank shot): a thin slab under a floating table top that overhangs it by 30 cm, 30 cm
		// above it. Its low sides take paint straight on (about 40 % of it); for its top, a ball fired straight in
		// must skim under the edge, while one fired at the ground just outside bounces once and skips in under the
		// table (or glances off its underside down onto the slab). A thicker slab spoils it: the bounced balls burst
		// on its side before they reach the top (measured: 39 % against 75 % in 10 s). Fired straight in, a ball must skim under the edge (only shallow, narrow lines reach the slab's
		// top); fired at the ground just outside, it bounces once and skips in under the table, or glances off its
		// underside down onto the slab.
		FPaintLevel Table = Level(TEXT("Table"), 50.0f, true);
		auto Slab = [&Table](const FVector& Min, const FVector& Max, EPaintBoxKind Kind, int32 Pal)
		{
			FPaintBox& B = Table.Boxes.AddDefaulted_GetRef();
			B.Min = Min;
			B.Max = Max;
			B.Kind = Kind;
			B.Palette = Pal;
		};
		Slab(FVector(-45, -45, 0), FVector(45, 45, 15), T, Cream);
		Slab(FVector(-75, -75, 45), FVector(75, 75, 60), K, Teal);
		Levels.Add(Table);
		Levels.Add(Keep);
		return Levels;
	}
}


ARainPaintGame::ARainPaintGame()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Surfaces = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Surfaces"));
	Surfaces->SetupAttachment(RootComponent);
	Surfaces->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surfaces->CastShadow = false;
	Rain = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Rain"));
	Rain->SetupAttachment(RootComponent);
	Rain->bAutoActivate = false;
	RainSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RainMagic/Niagara/NS_PaintRain.NS_PaintRain")));
	PaintBalls = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PaintBalls"));
	PaintBalls->SetupAttachment(RootComponent);
	PaintBalls->bAutoActivate = false;
	PaintBallSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RainMagic/Niagara/NS_PaintBalls.NS_PaintBalls")));
	PaintBallSystemGPU = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RainMagic/Niagara/NS_PaintBallsGPU.NS_PaintBallsGPU")));

	Levels = DefaultLevels();
}

void ARainPaintGame::BeginPlay()
{
	Super::BeginPlay();
	// The class may have loaded before BuildPaintAssets made these.
	if (!SurfaceMaterial)
	{
		SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RainMagic/Materials/M_PaintSurface.M_PaintSurface"));
	}
	if (!BackdropMaterial)
	{
		BackdropMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RainMagic/Materials/M_PaintBackdrop.M_PaintBackdrop"));
	}
	if (!Collection)
	{
		Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/RainMagic/Materials/MPC_Paint.MPC_Paint"));
	}
	if (!BallMaterial)
	{
		BallMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RainMagic/Materials/M_PaintBall.M_PaintBall"));
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Camera = GetWorld()->SpawnActor<ACameraActor>(GetActorLocation(), FRotator::ZeroRotator, Params);
	if (UCameraComponent* Cam = Camera ? Camera->GetCameraComponent() : nullptr)
	{
		Cam->SetProjectionMode(ECameraProjectionMode::Orthographic);
		Cam->SetAutoCalculateOrthoPlanes(false);
		Cam->SetOrthoNearClipPlane(1.0f);
		Cam->SetOrthoFarClipPlane(30000.0f);
		Cam->bConstrainAspectRatio = false;
		Cam->bOverrideAspectRatioAxisConstraint = true;
		Cam->AspectRatioAxisConstraint = AspectRatio_MaintainYFOV;

		// Flat colours straight to the screen: no tone curve, fixed exposure, none of the lens effects, no GI.
		FPostProcessSettings& PP = Cam->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 0.0f;
		PP.bOverride_ToneCurveAmount = true;
		PP.ToneCurveAmount = 0.0f;
		PP.bOverride_ExpandGamut = true;
		PP.ExpandGamut = 0.0f;
		PP.bOverride_BlueCorrection = true;
		PP.BlueCorrection = 0.0f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.0f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.0f;
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.0f;
		PP.bOverride_SceneFringeIntensity = true;
		PP.SceneFringeIntensity = 0.0f;
		PP.bOverride_LensFlareIntensity = true;
		PP.LensFlareIntensity = 0.0f;
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.0f;
		PP.bOverride_AmbientOcclusionIntensity = true;
		PP.AmbientOcclusionIntensity = 0.0f;
		PP.bOverride_DynamicGlobalIlluminationMethod = true;
		PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
		PP.bOverride_ReflectionMethod = true;
		PP.ReflectionMethod = EReflectionMethod::None;

		// The backdrop: a plane far behind everything, facing the camera, filling the view.
		Backdrop = NewObject<UStaticMeshComponent>(Camera, TEXT("Backdrop"));
		Backdrop->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
		Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Backdrop->CastShadow = false;
		Backdrop->SetupAttachment(Cam);
		Backdrop->SetRelativeLocationAndRotation(FVector(20000.0, 0.0, 0.0), FRotator(90.0, 0.0, 0.0));
		Backdrop->RegisterComponent();
		if (BackdropMaterial)
		{
			BackdropMID = UMaterialInstanceDynamic::Create(BackdropMaterial, this);
			Backdrop->SetMaterial(0, BackdropMID);
		}

		// The trackball, bottom right: a see-through cream globe with a dark arrow through it along the rain's direction.
		// It sits between the camera and the level (the view is orthographic: distance does not change its size).
		auto Part = [this, Cam](const TCHAR* Name, const TCHAR* Mesh)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Camera, Name);
			C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->CastShadow = false;
			C->SetupAttachment(Cam);
			C->RegisterComponent();
			return C;
		};
		Ball = Part(TEXT("Ball"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		BallShaft = Part(TEXT("BallShaft"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		BallTip = Part(TEXT("BallTip"), TEXT("/Engine/BasicShapes/Cone.Cone"));
		// Over the rain, always.
		Ball->SetTranslucentSortPriority(100);
		if (BallMaterial)
		{
			BallMID = UMaterialInstanceDynamic::Create(BallMaterial, this);
			Ball->SetMaterial(0, BallMID);
		}
	}
	// FXAA: flat colours and hard edges need no temporal upscaler. TSR cost 12.7 ms of a 14.8 ms frame here (M2 Pro,
	// 1080p) and smears moving drops; with FXAA the frame hits the display's 120 Hz cap.
	if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
	{
		PreviousAntiAliasing = AA->GetInt();
		AA->Set(1, ECVF_SetByCode);
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorHiddenInGame(true);
			Pawn->SetActorEnableCollision(false);
		}
		PC->SetViewTarget(Camera);
		PC->bShowMouseCursor = true;
		// The cursor stays visible and free while a button is held: by default the viewport hides it and switches to raw
		// mouse input during a drag, and the cursor's position then stands still (the trackball could not be dragged).
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		// The panel at the top left (APaintHUD reads this actor).
		PC->ClientSetHUD(APaintHUD::StaticClass());
	}
	// -PaintCount=N, -PaintBallCount=N: the pools from the start (measurements), so each system spawns once.
	int32 StartCount = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("PaintCount="), StartCount))
	{
		CVarPaintRainCount->Set(StartCount);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("PaintBallCount="), StartCount))
	{
		CVarPaintBallCount->Set(StartCount);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("PaintBallsGPU")))
	{
		CVarPaintBallsGPU->Set(1);
	}
	SetupRain();
	SetupPaintBalls();
	// -PaintLevel=N starts on level N (measurements, demos).
	int32 FirstLevel = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PaintLevel="), FirstLevel);
	StartLevel(FirstLevel);
	BallSpinDir = Direction();
	BallSpin = FQuat::FindBetweenNormals(FVector(0.0, 0.0, -1.0), BallSpinDir);
	PlaceBall();

	if (FParse::Param(FCommandLine::Get(), TEXT("PaintDemo")))
	{
		bQuitAfterDemo = FParse::Param(FCommandLine::Get(), TEXT("PaintDemoQuit"));
		PendingDemoDelay = 3.0f;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("PaintMeasure")) && GEngine)
	{
		// Uncapped, full resolution (the desktop default follows the display: 72.9% on a Retina Mac).
		for (const TCHAR* Cmd : { TEXT("t.MaxFPS 0"), TEXT("r.VSync 0"), TEXT("r.ScreenPercentage.Default.Desktop.Mode 0"), TEXT("r.ScreenPercentage 100"),
			TEXT("r.DynamicRes.OperationMode 0"), TEXT("stat unit") })
		{
			GEngine->Exec(GetWorld(), Cmd);
		}
		MeasureClock = 0.0f;
		StartRound();
	}
}

void ARainPaintGame::SetupRain()
{
	UNiagaraSystem* System = RainSystem.LoadSynchronous();
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("RainPaintGame: NS_PaintRain missing (run unreal.RainBuilderLibrary.build_paint_rain_assets())"));
		return;
	}
	RainCount = FMath::Max(CVarPaintRainCount.GetValueOnGameThread(), 0);
	Rain->SetAsset(System);
	Rain->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
	Rain->SetVariableObject(TEXT("RainHitHandler"), this);
	Rain->SetVariableInt(TEXT("RainGameCount"), RainCount);
	UploadRain();
	// A fresh instance: the burst fires once with the new pool (Activate(true) resets the instance, and the burst
	// would add a full new pool to the old drops).
	Rain->ReinitializeSystem();
	if (Levels.IsValidIndex(LevelIndex))
	{
		ResetRound();
		// Activating runs it again: pause it again if the balls are playing.
		ApplySystemMode();
	}
}

void ARainPaintGame::SetupPaintBalls()
{
	bPaintBallsGpu = CVarPaintBallsGPU.GetValueOnGameThread() != 0;
	UNiagaraSystem* System = (bPaintBallsGpu ? PaintBallSystemGPU : PaintBallSystem).LoadSynchronous();
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("RainPaintGame: %s missing (run unreal.RainBuilderLibrary.build_paint_ball_assets())"),
			bPaintBallsGpu ? TEXT("NS_PaintBallsGPU") : TEXT("NS_PaintBalls"));
		return;
	}
	PaintBallCount = FMath::Max(CVarPaintBallCount.GetValueOnGameThread(), 0);
	PaintBalls->SetAsset(System);
	PaintBalls->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
	PaintBalls->SetVariableObject(TEXT("RainHitHandler"), this);
	PaintBalls->SetVariableInt(TEXT("RainGameCount"), PaintBallCount);
	UploadRain();
	// A fresh instance, as for the rain (a reset would keep the old balls).
	PaintBalls->ReinitializeSystem();
	if (Surface.NumFaces() > 0)
	{
		UploadBoxes();
		ResetRound();
		ApplySystemMode();
	}
}

void ARainPaintGame::ApplySystemMode()
{
	// The system not played is paused and hidden (paused, it would still draw its last frame). rain.paint.PauseIdle 0
	// keeps it simulating, idle (its drops wait in the cloud), to measure what pausing saves.
	const bool bPause = CVarPaintPauseIdle.GetValueOnGameThread() != 0;
	PausedIdle = bPause;
	if (Rain)
	{
		Rain->SetPaused(bPause && bPaintBallMode);
		Rain->SetVisibility(!bPaintBallMode);
	}
	if (PaintBalls)
	{
		PaintBalls->SetPaused(bPause && !bPaintBallMode);
		PaintBalls->SetVisibility(bPaintBallMode);
	}
	UploadRain();
}

void ARainPaintGame::SetPaintBallMode(bool bBalls)
{
	if (Phase == EPaintPhase::Painting || bBalls == bPaintBallMode)
	{
		return;
	}
	bPaintBallMode = bBalls;
	ResetRound();
	ApplySystemMode();
}

int32 ARainPaintGame::GetLivePaintBallCount() const
{
	int32 Count = 0;
	if (FNiagaraSystemInstanceControllerConstPtr SIC = PaintBalls ? PaintBalls->GetSystemInstanceController() : nullptr)
	{
		if (const FNiagaraSystemInstance* Instance = SIC->GetSystemInstance_Unsafe())
		{
			for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
			{
				Count += Emitter->GetNumParticles();
			}
		}
	}
	return Count;
}

FString ARainPaintGame::DebugPaintBalls(int32 Max) const
{
	FString Out;
	FNiagaraSystemInstanceControllerConstPtr SIC = PaintBalls ? PaintBalls->GetSystemInstanceController() : nullptr;
	const FNiagaraSystemInstance* Instance = SIC ? SIC->GetSystemInstance_Unsafe() : nullptr;
	if (!Instance)
	{
		return TEXT("no paint ball instance");
	}
	for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
	{
		const FNiagaraDataSet& Data = Emitter->GetParticleData();
		const FNiagaraDataBuffer* Buffer = Data.GetCurrentData();
		const int32 Num = Buffer ? int32(Buffer->GetNumInstances()) : 0;
		auto Pos = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position"));
		auto Rnd = FNiagaraDataSetAccessor<FVector4f>::CreateReader(Data, TEXT("RainRand"));
		auto State = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("BallState"));
		auto Cycle = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("BallCycle"));
		auto Timer = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("BallTimer"));
		Out += FString::Printf(TEXT("%d balls\n"), Num);
		for (int32 i = 0; i < Num && i < Max; ++i)
		{
			const int32 j = i * FMath::Max(Num / FMath::Max(Max, 1), 1);
			Out += FString::Printf(TEXT("%d state %.0f cycle %.0f timer %.2f rnd %s pos %s\n"), j, State.Get(j), Cycle.Get(j), Timer.Get(j),
				*Rnd.Get(j).ToString(), *FVector3f(Pos.Get(j)).ToString());
		}
	}
	return Out;
}

FVector ARainPaintGame::Direction() const
{
	const double A = FMath::DegreesToRadians(SteerAzimuth);
	const double T = FMath::DegreesToRadians(SteerTilt);
	return FVector(FMath::Sin(T) * FMath::Cos(A), FMath::Sin(T) * FMath::Sin(A), -FMath::Cos(T));
}

void ARainPaintGame::SetDirectionVector(const FVector& D)
{
	const FVector N = D.GetSafeNormal();
	if (N.IsNearlyZero())
	{
		return;
	}
	SteerTilt = float(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(-N.Z, -1.0, 1.0))));
	if (FVector2D(N.X, N.Y).Size() > 1e-4)
	{
		SteerAzimuth = float(FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X)));
	}
}

int32 ARainPaintGame::GetBigActive() const
{
	const float Share = PoolSize > 0 ? float(GetPoolLeft()) / PoolSize : 1.0f;
	return FMath::RoundToInt(BigShare * RainCount * Share);
}

void ARainPaintGame::UploadShared(UNiagaraComponent* System, bool bActive)
{
	System->SetVariableVec3(TEXT("RainGameDir"), Direction());
	System->SetVariableVec3(TEXT("RainGameCenter"), StreamCenter);
	System->SetVariableFloat(TEXT("RainGameHalf"), StreamRadius);
	System->SetVariableFloat(TEXT("RainGameLength"), 2.0f * StreamRadius);
	System->SetVariableFloat(TEXT("RainGameActive"), bActive ? 1.0f : 0.0f);
	System->SetVariableFloat(TEXT("RainGameScoring"), bActive ? 1.0f : 0.0f);
	System->SetVariableFloat(TEXT("RainFreeze"), Freeze);
	System->SetVariableLinearColor(TEXT("RainPaintInk"), FLinearColor::FromSRGBColor(Levels[LevelIndex].Ink));
}

void ARainPaintGame::UploadRain()
{
	if (!Rain || !Levels.IsValidIndex(LevelIndex))
	{
		return;
	}
	const bool bOn = Phase == EPaintPhase::Painting;
	if (PaintBalls && PaintBalls->GetAsset())
	{
		UploadShared(PaintBalls, bOn && bPaintBallMode);
		PaintBalls->SetVariableFloat(TEXT("RainBallLeft"), PaintBallPool > 0 ? float(GetPaintBallsLeft()) / PaintBallPool : 1.0f);
		PaintBalls->SetVariableFloat(TEXT("RainBallWait"), FMath::Max(CVarPaintBallWait.GetValueOnGameThread(), 0.0f));
		PaintBalls->SetVariableFloat(TEXT("RainBallEpoch"), PaintBallEpoch);
	}
	UploadShared(Rain, bOn && !bPaintBallMode);
	Rain->SetVariableFloat(TEXT("RainPaintSpeed"), RainSpeed);
	Rain->SetVariableFloat(TEXT("RainPaintBigFrac"), BigShare);
	// The share of the pool left: as many drops fall, big and small (the big ones stay a fixed share of the rain).
	Rain->SetVariableFloat(TEXT("RainPaintLeft"), PoolSize > 0 ? float(GetPoolLeft()) / PoolSize : 1.0f);
	Rain->SetVariableFloat(TEXT("RainPaintCloudWait"), FMath::Max(CVarPaintCloudWait.GetValueOnGameThread(), 0.0f));
	Rain->SetVariableFloat(TEXT("RainPaintEpoch"), Epoch);
	Rain->SetVariableFloat(TEXT("RainPaintFilm"), bFilmDrops ? 1.0f : 0.0f);
	Rain->SetVariableFloat(TEXT("RainPaintOpacity"), DropOpacity);
	if (Camera)
	{
		Rain->SetVariableVec3(TEXT("RainViewFacing"), -Camera->GetActorForwardVector());
	}
}

void ARainPaintGame::UploadFullFaces()
{
	// One bit per face of every target box that takes no more paint: drops landing there are recycled on the GPU.
	if (!Rain)
	{
		return;
	}
	const TArray<int32> Indices = Surface.CollisionBoxIndices();
	for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
	{
		const float Full = Indices.IsValidIndex(i) ? float(Surface.FullFaces(Indices[i])) : 63.0f;
		Rain->SetVariableFloat(*FString::Printf(TEXT("RainBox%dFull"), i), Full);
		if (PaintBalls)
		{
			PaintBalls->SetVariableFloat(*FString::Printf(TEXT("RainBox%dFull"), i), Full);
		}
	}
}

void ARainPaintGame::StartRound()
{
	if (Phase == EPaintPhase::Painting)
	{
		return;
	}
	if (Phase == EPaintPhase::Done)
	{
		ResetRound();
	}
	Phase = EPaintPhase::Painting;
	SetLevelListOpen(false);
	Clock = 0.0f;
	UploadFullFaces();
	UploadRain();
}

void ARainPaintGame::ResetRound()
{
	Phase = EPaintPhase::Ready;
	bOutOfRain = false;
	Clock = 0.0f;
	// The pool is full again: every drop back to the cloud (a new epoch on the GPU, nothing spawned).
	PoolSize = FMath::RoundToInt(RainCount * (1.0f - BigShare));
	Consumed = 0;
	BigSeeds = 0;
	PaintFromSmall = PaintFromBig = 0.0;
	MaxHitsPerFrame = 0;
	FullExports = 0;
	Epoch += 1.0f;
	Freeze = 0.0f;
	bFreezeHeld = false;
	Surface.ClearPaint();
	Surface.Flush();
	// The paint each small drop carries: the level's budget (a multiple of what the goal needs) over the pool.
	PaintPerDrop = PoolSize > 0 ? PaintBudget * CoverageGoal * Surface.TargetAreaCm2() / PoolSize : 1.0f;
	// System 2's pool, the same way (its own budget).
	PaintBallPool = PaintBallCount;
	PaintBallsUsed = 0;
	PaintBallEpoch += 1.0f;
	// (At least a puddle of 6 cm: a ball is a big blob of paint, whatever the target's size.)
	PaintBallArea = PaintBallPool > 0 ? CVarPaintBallBudget.GetValueOnGameThread() * CoverageGoal * Surface.TargetAreaCm2() / PaintBallPool : 1.0f;
	PaintBallArea = FMath::Max(PaintBallArea, UE_PI * 36.0f);
	UploadFullFaces();
	UploadRain();
}

void ARainPaintGame::SetDirection(float Azimuth, float Tilt)
{
	SteerAzimuth = FMath::UnwindDegrees(Azimuth);
	SteerTilt = FMath::Clamp(Tilt, 0.0f, 180.0f);
	UploadRain();
}

void ARainPaintGame::BallPress()
{
	BallGrab(BallScreen);
}

void ARainPaintGame::BallRoll(float Dx, float Dy)
{
	BallDragTo(BallCursor + FVector2D(Dx, Dy));
}

void ARainPaintGame::BallRelease()
{
	bBallHeld = false;
	bBallByMouse = false;
	bFreezeHeld = false;
}

FVector ARainPaintGame::BallPoint(const FVector2D& Screen) const
{
	if (!Camera)
	{
		return FVector::ForwardVector;
	}
	// Screen y runs down; past the outline, the nearest point of the rim (the ball then turns about the view axis).
	FVector2D Q = (Screen - BallScreen) / FMath::Max(BallScreenRadius, 1.0f);
	Q.Y = -Q.Y;
	const double Out = Q.SizeSquared();
	if (Out > 1.0)
	{
		Q /= FMath::Sqrt(Out);
	}
	const double Z = FMath::Sqrt(FMath::Max(0.0, 1.0 - Q.SizeSquared()));
	return (Camera->GetActorRightVector() * Q.X + Camera->GetActorUpVector() * Q.Y - Camera->GetActorForwardVector() * Z).GetSafeNormal();
}

void ARainPaintGame::BallGrab(const FVector2D& Screen)
{
	bBallHeld = true;
	BallHeldSeconds = 0.0f;
	BallMoved = 0.0f;
	BallPressAt = BallCursor = Screen;
	BallGrabbed = BallPoint(Screen);
}

void ARainPaintGame::BallDragTo(const FVector2D& Screen)
{
	if (!bBallHeld || Screen.Equals(BallCursor, 0.01))
	{
		return;
	}
	BallMoved = FMath::Max(BallMoved, float(FVector2D::Distance(Screen, BallPressAt)));
	BallCursor = Screen;
	// A globe: the turn that carries the grabbed point of the surface to the point now under the cursor, applied to
	// the ball and to the arrow through it.
	const FVector To = BallPoint(Screen);
	const FQuat Turn = FQuat::FindBetweenNormals(BallGrabbed, To);
	BallGrabbed = To;
	BallSpin = (Turn * BallSpin).GetNormalized();
	SetDirectionVector(Turn.RotateVector(Direction()));
	BallSpinDir = Direction();
	UploadRain();
}

int32 ARainPaintGame::GetLiveParticleCount() const
{
	int32 Count = 0;
	if (FNiagaraSystemInstanceControllerConstPtr SIC = Rain ? Rain->GetSystemInstanceController() : nullptr)
	{
		if (const FNiagaraSystemInstance* Instance = SIC->GetSystemInstance_Unsafe())
		{
			for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
			{
				Count += Emitter->GetNumParticles();
			}
		}
	}
	return Count;
}

FString ARainPaintGame::GetLevelName() const
{
	return Levels.IsValidIndex(LevelIndex) ? Levels[LevelIndex].Name : FString();
}

float ARainPaintGame::GetParSeconds() const
{
	return Levels.IsValidIndex(LevelIndex) ? Levels[LevelIndex].ParSeconds : 0.0f;
}

FLinearColor ARainPaintGame::GetInkColor() const
{
	return Levels.IsValidIndex(LevelIndex) ? FLinearColor::FromSRGBColor(Levels[LevelIndex].Ink) : FLinearColor::Blue;
}

void ARainPaintGame::ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data, UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset)
{
	if (Phase != EPaintPhase::Painting || !Levels.IsValidIndex(LevelIndex))
	{
		return;
	}
	MaxHitsPerFrame = FMath::Max(MaxHitsPerFrame, Data.Num());
	if (Data.Num() >= RainShield::ExportCapacity)
	{
		// The export buffer was full: some consumed drops of that frame were not counted.
		++FullExports;
	}
	const FVector O = GetActorLocation();
	if (PaintBalls && NiagaraSystem == PaintBalls->GetAsset())
	{
		// System 2: a ball burst on the target. On paint it feeds that body; on bare surface it splashes a puddle.
		for (const FBasicParticleData& P : Data)
		{
			if (GetPaintBallsLeft() <= 0)
			{
				break;
			}
			++PaintBallsUsed;
			const FVector At = P.Position + SimulationPositionOffset - O;
			const FVector Dir = P.Velocity.IsNearlyZero() ? Direction() : P.Velocity.GetSafeNormal();
			if (!Surface.Feed(At, PaintBallArea))
			{
				Surface.AddPuddle(At, FMath::Sqrt(PaintBallArea / UE_PI), Dir);
			}
		}
		return;
	}
	for (const FBasicParticleData& P : Data)
	{
		// Size carries the kind of box hit (+ 10 for a small drop): only the target's faces take drops at all.
		const bool bSmall = P.Size > 9.5f;
		const FVector At = P.Position + SimulationPositionOffset - O;
		const FVector Dir = P.Velocity.IsNearlyZero() ? Direction() : P.Velocity.GetSafeNormal();
		if (bSmall)
		{
			// On paint it feeds that puddle and is used up; on bare surface it is not (it is already back in the cloud).
			if (GetPoolLeft() > 0 && Surface.Feed(At, PaintPerDrop))
			{
				++Consumed;
				PaintFromSmall += PaintPerDrop;
			}
		}
		else
		{
			// A big drop seeds a puddle (it is not used up).
			++BigSeeds;
			const float Area = BigSeedDrops * PaintPerDrop;
			PaintFromBig += Area;
			Surface.AddPuddle(At, FMath::Sqrt(Area / UE_PI), Dir);
		}
	}
}

void ARainPaintGame::TickMeasure(float DeltaSeconds)
{
	if (MeasureClock < 0.0f)
	{
		return;
	}
	const float RealDt = float(FApp::GetDeltaTime());
	MeasureClock += RealDt;
	float Settle = 4.0f;
	FParse::Value(FCommandLine::Get(), TEXT("PaintMeasureSettle="), Settle);
	if (MeasureClock < Settle)
	{
		return;
	}
	if (MeasureFrames == 0 && !FParse::Param(FCommandLine::Get(), TEXT("PaintMeasureNoNiagara")))
	{
		// The played system's own Niagara times (game thread, render thread, GPU), as stat Niagara counts them.
		MeasureListener = MakeShared<FParticlePerfStatsListener_GatherAll, ESPMode::ThreadSafe>(false, true, false);
		FParticlePerfStatsManager::AddListener(MeasureListener);
	}
	if (MeasureFrames == 0 && FParse::Param(FCommandLine::Get(), TEXT("PaintProfileGPU")))
	{
		// One frame's GPU passes to the log.
		GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
	}
	if (MeasureFrames == 0 && FParse::Param(FCommandLine::Get(), TEXT("PaintShot")))
	{
		// The window as it is (multiplier 1), for checking the framing against the editor's shots.
		// Through the player's console: HighResShot is the game viewport's command.
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=\"%s\""), *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Shots/paint_standalone.png"))));
		}
	}
	++MeasureFrames;
	MeasureFrameMs += RealDt * 1000.0;
	if (const FStatUnitData* Unit = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetStatUnitData() : nullptr)
	{
		MeasureGameMs += Unit->RawGameThreadTime;
		MeasureRenderMs += Unit->RawRenderThreadTime;
		MeasureGpuMs += Unit->RawGPUFrameTime[0];
	}
	if (MeasureClock >= Settle + 5.0f)
	{
		const double N = FMath::Max(MeasureFrames, 1);
		double NiagaraGt = 0.0, NiagaraRt = 0.0, NiagaraGpu = 0.0;
		UNiagaraComponent* Played = bPaintBallMode ? PaintBalls : Rain;
#if WITH_PER_SYSTEM_PARTICLE_PERF_STATS
		if (FAccumulatedParticlePerfStats* Stats = MeasureListener && Played ? MeasureListener->GetStats(Played->GetAsset()) : nullptr)
		{
			// Per frame, in microseconds.
			NiagaraGt = Stats->GetGameThreadStats().GetPerFrameAvg() / 1000.0;
			NiagaraRt = Stats->GetRenderThreadStats(true).GetPerFrameAvg() / 1000.0;
			NiagaraGpu = Stats->GetGPUStats(true).GetPerFrameAvgMicroseconds() / 1000.0;
		}
#endif
		if (MeasureListener)
		{
			FParticlePerfStatsManager::RemoveListener(MeasureListener.Get());
			MeasureListener.Reset();
		}
		const FString Line = FString::Printf(TEXT("PAINTMEASURE level=%d system=%d%s drops=%d balls=%d live=%d frames=%d frame_ms=%.2f game_ms=%.2f render_ms=%.2f gpu_ms=%.2f niagara_gt_ms=%.3f niagara_rt_ms=%.3f niagara_gpu_ms=%.3f puddles=%d coverage=%.3f pool_left=%d balls_left=%d"),
			LevelIndex, bPaintBallMode ? 2 : 1, bPaintBallMode ? (bPaintBallsGpu ? TEXT("gpu") : TEXT("cpu")) : TEXT(""), RainCount, PaintBallCount,
			bPaintBallMode ? GetLivePaintBallCount() : GetLiveParticleCount(), MeasureFrames,
			MeasureFrameMs / N, MeasureGameMs / N, MeasureRenderMs / N, MeasureGpuMs / N, NiagaraGt, NiagaraRt, NiagaraGpu,
			Surface.NumPuddles(), GetCoverage(), GetPoolLeft(), GetPaintBallsLeft());
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		FFileHelper::SaveStringToFile(Line + TEXT("\n"), *(FPaths::ProjectSavedDir() / TEXT("Sweeps/PaintMeasure.txt")), FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
		MeasureClock = -1.0f;
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->ConsoleCommand(TEXT("quit"));
		}
	}
}

void ARainPaintGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (MeasureListener)
	{
		FParticlePerfStatsManager::RemoveListener(MeasureListener.Get());
		MeasureListener.Reset();
	}
	if (PreviousAntiAliasing >= 0)
	{
		if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
		{
			AA->Set(PreviousAntiAliasing, ECVF_SetByCode);
		}
	}
	Super::EndPlay(Reason);
}

void ARainPaintGame::SetLevelRow(int32 Index, const FBox2D& Rect)
{
	if (Index >= 0)
	{
		if (LevelRows.Num() <= Index)
		{
			LevelRows.SetNum(Index + 1);
		}
		LevelRows[Index] = Rect;
	}
}

void ARainPaintGame::SetLevelListOpen(bool bOpen)
{
	bLevelListOpen = bOpen && Phase != EPaintPhase::Painting;
	if (!bLevelListOpen)
	{
		LevelRows.Reset();
	}
}

bool ARainPaintGame::ClickLevelList(FVector2D Screen)
{
	for (int32 i = 0; bLevelListOpen && i < LevelRows.Num() && i < Levels.Num(); ++i)
	{
		if (LevelRows[i].bIsValid && LevelRows[i].IsInside(Screen))
		{
			StartLevel(i);
			SetLevelListOpen(false);
			return true;
		}
	}
	return false;
}

void ARainPaintGame::StartLevel(int32 Index)
{
	if (Levels.Num() == 0)
	{
		return;
	}
	LevelIndex = (Index % Levels.Num() + Levels.Num()) % Levels.Num();
	ViewHeight = Levels[LevelIndex].ViewHeight;
	Phase = EPaintPhase::Ready;
	bPaintBallMode = Levels[LevelIndex].bPaintBalls || FParse::Param(FCommandLine::Get(), TEXT("PaintBalls"));
	BuildLevel();
	ApplySystemMode();
	UpdateCamera();
}

void ARainPaintGame::BuildLevel()
{
	const FPaintLevel& L = Levels[LevelIndex];
	const double T0 = FPlatformTime::Seconds();

	// Built in the actor's space (the mesh component sits at the actor; the actor is not rotated or scaled).
	const FVector O = GetActorLocation();
	Surface.Build(L.Boxes, L.Arches, L.Openings, this);
	Atlas = Surface.GetAtlas();
	Surface.BuildMesh(Surfaces, L.Palettes.Num(), L.Spires, L.Ribbons);
	BuildSeconds = FPlatformTime::Seconds() - T0;

	// The rain's stream: a cylinder around the level's bounding sphere, along any direction.
	const FBox Bounds = Surface.Bounds();
	StreamCenter = O + Bounds.GetCenter();
	StreamRadius = float(Bounds.GetExtent().Size()) + 20.0f;
	UploadBoxes();
	ResetRound();

	// One material per palette for the faces (with the atlas) and one for the spires (without).
	Materials.Reset();
	auto Lin = [](const FColor& C) { return FLinearColor::FromSRGBColor(C); };
	auto Make = [this, &L, &Lin, &O](const FPaintPalette& P, bool bAtlas)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
		M->SetTextureParameterValue(TEXT("Atlas"), Atlas);
		M->SetScalarParameterValue(TEXT("UseAtlas"), bAtlas ? 1.0f : 0.0f);
		M->SetVectorParameterValue(TEXT("Top"), Lin(P.Top));
		M->SetVectorParameterValue(TEXT("Light"), Lin(P.Light));
		M->SetVectorParameterValue(TEXT("Dark"), Lin(P.Dark));
		M->SetVectorParameterValue(TEXT("Bottom"), Lin(P.Bottom));
		M->SetVectorParameterValue(TEXT("AOColor"), Lin(P.AO));
		M->SetVectorParameterValue(TEXT("GradColor"), Lin(P.Grad));
		M->SetVectorParameterValue(TEXT("Tile"), FLinearColor(P.TileSize, P.TileLine, P.TileDarken, O.Z + P.TileMaxZ));
		M->SetVectorParameterValue(TEXT("Grad"), FLinearColor(O.Z + L.GradRange.X, O.Z + L.GradRange.Y, P.GradAmount, 0.0f));
		M->SetVectorParameterValue(TEXT("OpeningColor"), Lin(P.Opening));
		FLinearColor Rim = Lin(P.OpeningRim);
		Rim.A = P.OpeningRimWidth;
		M->SetVectorParameterValue(TEXT("OpeningRim"), Rim);
		FLinearColor Barrel = Lin(P.OpeningBarrel);
		Barrel.A = P.OpeningShift;
		M->SetVectorParameterValue(TEXT("OpeningBarrel"), Barrel);
		M->SetVectorParameterValue(TEXT("AtlasTexel"), FLinearColor(1.0f / FMath::Max(Surface.GetAtlasWidth(), 1), 1.0f / FMath::Max(Surface.GetAtlasHeight(), 1), Surface.TexelSize, 0.0f));
		FLinearColor Edge = Lin(P.Edge);
		Edge.A = P.EdgeAmount;
		M->SetVectorParameterValue(TEXT("EdgeColor"), Edge);
		M->SetVectorParameterValue(TEXT("InkColor"), Lin(L.Ink));
		M->SetVectorParameterValue(TEXT("Fade"), FLinearColor(O.Z + L.FadeRange.X, O.Z + L.FadeRange.Y, L.FadeAmount, 0.0f));
		M->SetVectorParameterValue(TEXT("FadeColor"), Lin(L.BackGlow));
		Materials.Add(M);
		return M;
	};
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 p = 0; p < L.Palettes.Num(); ++p)
		{
			Surfaces->SetMaterial(Pass * L.Palettes.Num() + p, Make(L.Palettes[p], Pass == 0));
		}
	}
	// The trackball's arrow: navy (apart from the paint's blue), opaque inside the see-through ball.
	if (Ball && BallShaft && BallTip && L.Palettes.Num() > 0)
	{
		UMaterialInstanceDynamic* Arrow = Make(Palette(TEXT("Arrow"), TEXT("3c4f6e"), TEXT("2e4060"), TEXT("1c2a44"), TEXT("121c30"), TEXT("ffffff"), TEXT("000000"), TEXT("ffffff"), 0.0f), false);
		BallShaft->SetMaterial(0, Arrow);
		BallTip->SetMaterial(0, Arrow);
	}
	if (BackdropMID)
	{
		BackdropMID->SetVectorParameterValue(TEXT("Center"), Lin(L.BackCenter));
		BackdropMID->SetVectorParameterValue(TEXT("Edge"), Lin(L.BackEdge));
		BackdropMID->SetVectorParameterValue(TEXT("Glow"), Lin(L.BackGlow));
		BackdropMID->SetVectorParameterValue(TEXT("GlowPos"), FLinearColor(L.BackGlowPos.X, L.BackGlowPos.Y, L.BackGlowPos.Z, L.BackGlowPos.W));
	}
}

void ARainPaintGame::UploadBoxes()
{
	// The level's collision boxes for both systems (world space): kind, extent.
	const FVector O = GetActorLocation();
	const TArray<FPaintBox> Collide = Surface.CollisionBoxes();
	BoxesUsed = Collide.Num();
	if (BoxesUsed > RainShield::MaxBoxes && Levels.IsValidIndex(LevelIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("RainPaintGame: level %s has %d collision boxes, the rain tests only %d"), *Levels[LevelIndex].Name, BoxesUsed, RainShield::MaxBoxes);
	}
	for (UNiagaraComponent* System : { Rain.Get(), PaintBalls.Get() })
	{
		if (!System)
		{
			continue;
		}
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			const bool bUsed = Collide.IsValidIndex(i);
			const float Kind = !bUsed ? RainShield::BoxNone : Collide[i].Kind == EPaintBoxKind::Target ? RainShield::BoxProtected
				: Collide[i].Kind == EPaintBoxKind::Blocker ? RainShield::BoxBlocker : KindGround;
			System->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMin"), i), bUsed ? O + Collide[i].Min : FVector(1e6));
			System->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMax"), i), bUsed ? O + Collide[i].Max : FVector(-1e6));
			System->SetVariableFloat(*FString::Printf(TEXT("RainBox%dKind"), i), Kind);
		}
	}
}

void ARainPaintGame::SetView(float Yaw, float Height)
{
	ViewYaw = TargetYaw = Yaw;
	if (Height > 0.0f)
	{
		ViewHeight = Height;
	}
	UpdateCamera();
}

void ARainPaintGame::UpdateCamera()
{
	if (!Camera || Levels.Num() == 0)
	{
		return;
	}
	const FRotator Rot(ViewPitch, ViewYaw, 0.0f);
	const FVector Target = GetActorLocation() + Levels[LevelIndex].ViewTarget;
	Camera->SetActorLocationAndRotation(Target - Rot.Vector() * 5000.0, Rot);
	// The level frames a height; the orthographic width follows the window's aspect ratio.
	FVector2D Size(16.0, 9.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	ViewAspect = float(Size.X / FMath::Max(Size.Y, 1.0));
	if (UCameraComponent* Cam = Camera->GetCameraComponent())
	{
		// Keeping the vertical extent (the camera below is set to MaintainYFOV), an orthographic view's "width" is its
		// height (CameraStackTypes.cpp: half height = OrthoWidth / 2, half width times the aspect ratio).
		Cam->SetOrthoWidth(ViewHeight);
	}
	if (Backdrop)
	{
		// Larger than any window shows, at the backdrop's distance (the view is orthographic).
		Backdrop->SetRelativeScale3D(FVector(ViewHeight * 4.0 / 100.0, ViewHeight * 8.0 / 100.0, 1.0));
	}
	// The island's shadow on the backdrop, under its bottom corner nearest the camera.
	if (BackdropMID)
	{
		const FPaintLevel& L = Levels[LevelIndex];
		const FVector Fwd = Rot.Vector();
		const FVector Corner = L.ShadowAt + FVector(-FMath::Sign(Fwd.X) * L.ShadowHalf.X, -FMath::Sign(Fwd.Y) * L.ShadowHalf.Y, 0.0);
		const FVector Rel = GetActorLocation() + Corner - Camera->GetActorLocation();
		BackdropMID->SetVectorParameterValue(TEXT("Shadow"), FLinearColor(Rel.X, Rel.Y, Rel.Z, 0.0f));
		BackdropMID->SetVectorParameterValue(TEXT("ShadowSize"), FLinearColor(L.ShadowSize.X, L.ShadowSize.Y, 0.0f, 0.0f));
		FLinearColor Shadow = FLinearColor::FromSRGBColor(L.ShadowColor);
		Shadow.A = L.ShadowAmount;
		BackdropMID->SetVectorParameterValue(TEXT("ShadowColor"), Shadow);
	}
	// The ball's shine: from the upper left of the view, in front.
	if (BallMID)
	{
		const FRotationMatrix View(Rot);
		const FVector Shine = (-View.GetUnitAxis(EAxis::X) + 0.75 * View.GetUnitAxis(EAxis::Z) - 0.55 * View.GetUnitAxis(EAxis::Y)).GetSafeNormal();
		BallMID->SetVectorParameterValue(TEXT("Shine"), FLinearColor(Shine.X, Shine.Y, Shine.Z, 0.0f));
	}
	// Key direction: the camera's right, so the face on the right of the view is the light one.
	if (Collection && GetWorld())
	{
		if (UMaterialParameterCollectionInstance* MPC = GetWorld()->GetParameterCollectionInstance(Collection))
		{
			const FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
			MPC->SetVectorParameterValue(TEXT("Key"), FLinearColor(Right.X, Right.Y, 0.0f, 0.0f));
		}
	}
	PlaceBall();
}

void ARainPaintGame::PlaceBall()
{
	if (!Camera || !Ball || !BallShaft || !BallTip)
	{
		return;
	}
	// Bottom right of the view, in the camera's space (x forward, y right, z up), sized with the view's height.
	const float R = BallSize * ViewHeight;
	const float HalfW = 0.5f * ViewHeight * FMath::Max(ViewAspect, 0.5f);
	const float HalfH = 0.5f * ViewHeight;
	Ball->SetRelativeLocation(FVector(1000.0, HalfW - 1.7f * R, -HalfH + 1.7f * R));
	Ball->SetRelativeScale3D(FVector(R / 50.0f));
	// The globe turns with the rain however it was turned (keys, scripts): its -Z pole follows the direction.
	const FVector Now = Direction();
	if (!BallSpinDir.IsZero() && !Now.Equals(BallSpinDir, 1e-5))
	{
		BallSpin = (FQuat::FindBetweenNormals(BallSpinDir, Now) * BallSpin).GetNormalized();
	}
	BallSpinDir = Now;
	Ball->SetWorldRotation(BallSpin);
	// The arrow along the rain's direction through the ball's centre: the shaft (a cylinder, 100 tall, radius 50,
	// along z) and the tip (a cone, the same size) at the end the rain goes to.
	const FVector C = Ball->GetComponentLocation();
	const FVector D = Direction();
	const FQuat Along = FRotationMatrix::MakeFromZ(D).ToQuat();
	BallShaft->SetWorldLocationAndRotation(C - D * (0.25f * R), Along);
	BallShaft->SetWorldScale3D(FVector(0.16f * R / 50.0f, 0.16f * R / 50.0f, 1.9f * R / 100.0f));
	BallTip->SetWorldLocationAndRotation(C + D * (0.95f * R), Along);
	BallTip->SetWorldScale3D(FVector(0.42f * R / 50.0f, 0.42f * R / 50.0f, 0.65f * R / 100.0f));
	// Where it is on screen, for the mouse.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector2D Screen;
		if (PC->ProjectWorldLocationToScreen(C, Screen))
		{
			BallScreen = Screen;
		}
		FVector2D Size(16.0, 9.0);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Size);
		}
		BallScreenRadius = R / ViewHeight * float(Size.Y);
	}
}

void ARainPaintGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HandleInput(DeltaSeconds);
	TickMeasure(DeltaSeconds);
	if (PendingDemoDelay > 0.0f)
	{
		PendingDemoDelay -= float(FApp::GetDeltaTime());
		if (PendingDemoDelay <= 0.0f)
		{
			StartDemo();
		}
	}
	TickDemo(DeltaSeconds);
	if (Rain && FMath::Max(CVarPaintRainCount.GetValueOnGameThread(), 0) != RainCount)
	{
		SetupRain();
	}
	if (PaintBalls && PaintBalls->GetAsset() && (FMath::Max(CVarPaintBallCount.GetValueOnGameThread(), 0) != PaintBallCount
		|| (CVarPaintBallsGPU.GetValueOnGameThread() != 0) != bPaintBallsGpu))
	{
		SetupPaintBalls();
	}
	if (Rain && CVarPaintBigShare.GetValueOnGameThread() != BigShareSet)
	{
		// The drops are big or small from their spawn: a new share needs a new pool.
		BigShareSet = CVarPaintBigShare.GetValueOnGameThread();
		BigShare = FMath::Clamp(BigShareSet, 0.0f, 0.5f);
		SetupRain();
	}
	BigSeedDrops = CVarPaintBigSeed.GetValueOnGameThread();
	DropOpacity = FMath::Clamp(CVarPaintDropOpacity.GetValueOnGameThread(), 0.0f, 2.0f);
	if ((CVarPaintPauseIdle.GetValueOnGameThread() != 0) != PausedIdle)
	{
		ApplySystemMode();
	}
	if (CVarPaintBudget.GetValueOnGameThread() != PaintBudget)
	{
		PaintBudget = CVarPaintBudget.GetValueOnGameThread();
		PaintPerDrop = PoolSize > 0 ? PaintBudget * CoverageGoal * Surface.TargetAreaCm2() / PoolSize : 1.0f;
	}
	// The freeze eases in while the ball is held still, and out when it is let go.
	if (bBallHeld)
	{
		BallHeldSeconds += DeltaSeconds;
		// Still: within a twelfth of the ball's radius on screen (a hand on a free cursor jitters a few pixels).
		bFreezeHeld = bFreezeHeld || (BallHeldSeconds >= FreezePressSeconds && BallMoved < FMath::Max(6.0f, 0.08f * BallScreenRadius));
	}
	Freeze += ((bFreezeHeld ? 1.0f : 0.0f) - Freeze) * (1.0f - FMath::Exp(-DeltaSeconds / FMath::Max(FreezeEaseSeconds, 0.01f)));
	// Measuring: a pool rebuilt by a count cvar resets the round; start it again.
	if (MeasureClock >= 0.0f && Phase == EPaintPhase::Ready)
	{
		StartRound();
	}
	// Held rain holds time: the clock and the spreading paint stop with the drops.
	const float GameSeconds = DeltaSeconds * (1.0f - Freeze);
	if (Phase == EPaintPhase::Painting)
	{
		Clock += GameSeconds;
		// Out of rain once the last paint bought has shown (the coverage may still reach the goal), or 3 s after the
		// pool ran dry at the latest.
		const bool bDry = bPaintBallMode ? GetPaintBallsLeft() <= 0 : GetPoolLeft() <= 0;
		DryClock = bDry ? DryClock + GameSeconds : 0.0f;
		const bool bEmpty = bDry && (Surface.IsSettled() || DryClock > 3.0f);
		if (MeasureClock < 0.0f && (GetCoverage() >= CoverageGoal || bEmpty))
		{
			bOutOfRain = GetCoverage() < CoverageGoal;
			Phase = EPaintPhase::Done;
		}
		FullFacesClock += DeltaSeconds;
		if (FullFacesClock >= 0.2f)
		{
			FullFacesClock = 0.0f;
			UploadFullFaces();
		}
	}
	Surface.Tick(GameSeconds);
	Surface.Flush();
	UploadRain();
	FVector2D Size(16.0, 9.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	if (!FMath::IsNearlyEqual(ViewAspect, float(Size.X / FMath::Max(Size.Y, 1.0)), 1e-3f))
	{
		UpdateCamera();
	}
	if (!FMath::IsNearlyEqual(ViewYaw, TargetYaw, 0.01f))
	{
		// Turn the view smoothly to the next quarter.
		ViewYaw = FMath::FInterpTo(ViewYaw, TargetYaw, DeltaSeconds, 8.0f);
		UpdateCamera();
	}
	PlaceBall();
	// The rain's direction in the level: a navy arrow coming in from upwind (apart from the paint's blue).
	const FVector D = Direction();
	DrawDebugDirectionalArrow(GetWorld(), StreamCenter - D * (0.95f * StreamRadius), StreamCenter - D * (0.6f * StreamRadius), 30.0f, FColor(0x2a, 0x3a, 0x58), false, -1.0f, SDPG_Foreground, 4.0f);
}

void ARainPaintGame::HandleInput(float DeltaSeconds)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	auto Pressed = [PC](const FKey& Key) { return PC->WasInputKeyJustPressed(Key); };
	if (Pressed(EKeys::Q)) TargetYaw += 90.0f;
	if (Pressed(EKeys::E)) TargetYaw -= 90.0f;
	if (Pressed(EKeys::MouseScrollUp)) { ViewHeight = FMath::Max(ViewHeight * 0.9f, 150.0f); UpdateCamera(); }
	if (Pressed(EKeys::MouseScrollDown)) { ViewHeight = FMath::Min(ViewHeight / 0.9f, 2000.0f); UpdateCamera(); }
	if (Pressed(EKeys::PageUp)) StartLevel(LevelIndex + 1);
	if (Pressed(EKeys::PageDown)) StartLevel(LevelIndex - 1);

	float MX = 0.0f, MY = 0.0f;
	const bool bHaveMouse = PC->GetMousePosition(MX, MY);
	const FVector2D Mouse(MX, MY);

	// The level select, before a round: the panel's level button or L opens the list, a row picks its level, a click
	// anywhere else (or L again) closes it; the number keys pick a level directly.
	if (Phase != EPaintPhase::Painting)
	{
		static const FKey NumberKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		for (int32 i = 0; i < UE_ARRAY_COUNT(NumberKeys) && i < Levels.Num(); ++i)
		{
			if (Pressed(NumberKeys[i]))
			{
				StartLevel(i);
				SetLevelListOpen(false);
			}
		}
		const bool bClick = bHaveMouse && Pressed(EKeys::LeftMouseButton);
		if (Pressed(EKeys::L) || (bClick && LevelButton.bIsValid && LevelButton.IsInside(Mouse)))
		{
			SetLevelListOpen(!bLevelListOpen);
			return;
		}
		if (bClick && bLevelListOpen)
		{
			// A click while the list is open only picks a row or closes the list.
			if (!ClickLevelList(Mouse))
			{
				SetLevelListOpen(false);
			}
			return;
		}
	}

	// The panel's sliders: grab one, drag along it; the pool's size is applied when it is let go (a new pool).
	if (bHaveMouse && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		for (int32 s = 0; s < 2; ++s)
		{
			if (SliderRects[s].bIsValid && SliderRects[s].ExpandBy(FVector2D(0.0, 6.0)).IsInside(Mouse))
			{
				DraggingSlider = s;
				PoolSliderCount = bPaintBallMode ? PaintBallCount : RainCount;
			}
		}
	}
	if (DraggingSlider != INDEX_NONE)
	{
		const FBox2D& R = SliderRects[DraggingSlider];
		const float Value = R.bIsValid ? FMath::Clamp(float((MX - R.Min.X) / FMath::Max(R.GetSize().X, 1.0)), 0.0f, 1.0f) : GetSliderValue(DraggingSlider);
		if (DraggingSlider == 0)
		{
			PoolSliderCount = SliderToCount(Value, bPaintBallMode);
		}
		else
		{
			SetDropOpacity(2.0f * Value);
		}
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			if (DraggingSlider == 0 && Phase != EPaintPhase::Painting)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(bPaintBallMode ? TEXT("rain.paint.Balls") : TEXT("rain.paint.Count")))
				{
					Var->Set(PoolSliderCount, ECVF_SetByConsole);
				}
			}
			DraggingSlider = INDEX_NONE;
		}
	}

	// The trackball: grab it, drag its surface to turn the rain, hold it still to freeze the rain.
	if (bHaveMouse && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && FVector2D::Distance(Mouse, BallScreen) <= BallScreenRadius * 1.15f)
	{
		BallGrab(Mouse);
		bBallByMouse = true;
	}
	if (bBallHeld && bBallByMouse)
	{
		if (!PC->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			BallRelease();
		}
		else if (bHaveMouse)
		{
			BallDragTo(Mouse);
		}
	}

	// Keys: A/D (or left/right) turn the rain around the vertical, W/S (or up/down) tilt it.
	auto Axis = [PC](const FKey& Plus, const FKey& Plus2, const FKey& Minus, const FKey& Minus2)
	{
		return (PC->IsInputKeyDown(Plus) || PC->IsInputKeyDown(Plus2) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(Minus) || PC->IsInputKeyDown(Minus2) ? 1.0f : 0.0f);
	};
	SteerAzimuth = FMath::UnwindDegrees(SteerAzimuth + SteerYawRate * DeltaSeconds * Axis(EKeys::D, EKeys::Right, EKeys::A, EKeys::Left));
	SteerTilt = FMath::Clamp(SteerTilt + SteerTiltRate * DeltaSeconds * Axis(EKeys::W, EKeys::Up, EKeys::S, EKeys::Down), 0.0f, 180.0f);
	if (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar))
	{
		if (Phase == EPaintPhase::Done && Pressed(EKeys::Enter) && !bOutOfRain)
		{
			StartLevel(LevelIndex + 1);
		}
		else
		{
			StartRound();
		}
	}
	if (Pressed(EKeys::R)) ResetRound();
	// The drops' look, before a round: F, or the button on the panel.
	const bool bButton = bHaveMouse && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && LookButton.bIsValid && LookButton.IsInside(Mouse);
	if (Phase != EPaintPhase::Painting && (Pressed(EKeys::F) || bButton))
	{
		SetFilmDrops(!bFilmDrops);
	}
	// The system: rain (System 1) or paint balls (System 2), before a round.
	const bool bSystemButton = bHaveMouse && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && SystemButton.bIsValid && SystemButton.IsInside(Mouse);
	if (Pressed(EKeys::Tab) || Pressed(EKeys::M) || bSystemButton)
	{
		SetPaintBallMode(!bPaintBallMode);
	}
}

float ARainPaintGame::GetSliderValue(int32 Slider) const
{
	if (Slider == 0)
	{
		return CountToSlider(DraggingSlider == 0 ? PoolSliderCount : (bPaintBallMode ? PaintBallCount : RainCount), bPaintBallMode);
	}
	return 0.5f * DropOpacity;
}

FString ARainPaintGame::GetSliderLabel(int32 Slider) const
{
	if (Slider == 0)
	{
		const int32 Count = DraggingSlider == 0 ? PoolSliderCount : (bPaintBallMode ? PaintBallCount : RainCount);
		return FString::Printf(TEXT("%s %s%s"), bPaintBallMode ? TEXT("paint balls") : TEXT("drops"), *FText::AsNumber(Count).ToString(),
			DraggingSlider == 0 ? TEXT("   (let go to apply)") : TEXT(""));
	}
	return FString::Printf(TEXT("drop opacity %.0f%%"), 100.0f * DropOpacity);
}

void ARainPaintGame::SetDropOpacity(float Opacity)
{
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("rain.paint.DropOpacity")))
	{
		Var->Set(FMath::Clamp(Opacity, 0.0f, 2.0f), ECVF_SetByConsole);
	}
}

void ARainPaintGame::SetFilmDrops(bool bFilm)
{
	bFilmDrops = bFilm;
	UploadRain();
}

FString ARainPaintGame::Describe() const
{
	return FString::Printf(TEXT("level=%d faces=%d texels=%d atlas=%dx%d build=%.3fs yaw=%.1f height=%.0f aspect=%.3f collide=%d target=%.0fcm2 phase=%d out=%d clock=%.2f coverage=%.4f pool=%d left=%d consumed=%d bigactive=%d bigseeds=%d paint_small=%.0f paint_big=%.0f per_drop=%.3f maxhits=%d full_exports=%d drops=%d alive=%d puddles=%d growing=%d freeze=%.2f dir=(%.2f,%.2f,%.2f) system=%d balls=%d balls_left=%d balls_alive=%d"),
		LevelIndex, Surface.NumFaces(), Surface.NumTexels(), Surface.GetAtlasWidth(), Surface.GetAtlasHeight(), BuildSeconds, ViewYaw, ViewHeight, ViewAspect,
		BoxesUsed, Surface.TargetAreaCm2(), int32(Phase), bOutOfRain ? 1 : 0, Clock, GetCoverage(), PoolSize, GetPoolLeft(), Consumed, GetBigActive(), BigSeeds,
		PaintFromSmall, PaintFromBig, PaintPerDrop, MaxHitsPerFrame, FullExports, RainCount, GetLiveParticleCount(), Surface.NumPuddles(), Surface.NumGrowing(), Freeze,
		Direction().X, Direction().Y, Direction().Z, bPaintBallMode ? 2 : 1, PaintBallPool, GetPaintBallsLeft(), GetLivePaintBallCount());
}

void ARainPaintGame::StartDemo()
{
	auto Exec = [this](const TCHAR* Command) { GEngine->Exec(GetWorld(), Command); };
	auto Say = [this](const TCHAR* Text) { return [this, Text]() { DemoCaption = Text; }; };
	// A level with one system, aimed, the round running.
	auto Play = [this](int32 Level, bool bBalls, float Azimuth, float Tilt, float Spin)
	{
		return [this, Level, bBalls, Azimuth, Tilt, Spin]()
		{
			StartLevel(Level);
			SetPaintBallMode(bBalls);
			SetDirection(Azimuth, Tilt);
			DemoSpin = Spin;
			StartRound();
		};
	};
	// A new pool respawns the system (as Tick would on the cvar) and resets the round: start it again.
	auto Count = [this, Exec](const TCHAR* Command, bool bBalls)
	{
		return [this, Exec, Command, bBalls]() { Exec(Command); bBalls ? SetupPaintBalls() : SetupRain(); StartRound(); };
	};
	DemoSteps = {
		{ 0.0f, [this, Exec]() { Exec(TEXT("rain.paint.Count 100000")); Exec(TEXT("rain.paint.Balls 3500")); Exec(TEXT("rain.paint.BallsGPU 0"));
			Exec(TEXT("fx.Niagara.Debug.Hud OverviewLocation=30,520")); SetFilmDrops(false); } },
		{ 0.0f, Play(7, false, 30.0f, 30.0f, 14.0f) },
		{ 0.0f, Say(TEXT("Paint Valley: steer a rain of paint until it covers the cream blocks")) },
		{ 8.0f, Play(0, false, 45.0f, 25.0f, 20.0f) },
		{ 8.0f, Say(TEXT("System 1: 100,000 GPU drops. Every landing is read back from the GPU and paints the surface")) },
		{ 16.0f, Say(TEXT("Paint spreads as puddles that join. Drops that land on paint grow it")) },
		{ 21.0f, [this]() { DemoSpin = 0.0f; BallPress(); } },
		{ 21.0f, Say(TEXT("Hold the trackball still: the rain freezes and the clock stops")) },
		{ 25.0f, [this]() { BallRelease(); } },
		// Canopy: the roof overhangs the long block by a cell, two cells above it; from the open side (+Y), tilted low.
		{ 26.0f, Play(1, false, -90.0f, 70.0f, 0.0f) },
		{ 26.0f, Say(TEXT("Canopy: tilt the rain low to reach under the roof")) },
		{ 33.0f, [this]() { SetFilmDrops(true); } },
		{ 33.0f, Say(TEXT("Film look (F): each drop is a tiny lens that shows the scene behind it")) },
		{ 40.0f, [this, Exec]() { SetFilmDrops(false); Exec(TEXT("rain.paint.BallWait 8")); } },
		// Table: aimed at the ground just outside, the balls bounce once and skip in under the table top.
		{ 40.0f, Play(6, true, 0.0f, 55.0f, 0.0f) },
		{ 40.0f, Say(TEXT("System 2: 3,500 CPU paint balls. They bounce off the level and burst on the target")) },
		{ 46.0f, Say(TEXT("A bank shot: aimed at the ground, the balls skip in under the table top")) },
		{ 52.0f, Play(4, true, 30.0f, 35.0f, 20.0f) },
		{ 52.0f, Say(TEXT("Balls collide with their nearest neighbours: a Particle Attribute Reader query on the CPU")) },
		{ 60.0f, Play(0, false, 45.0f, 25.0f, 20.0f) },
		{ 60.0f, [Exec]() { Exec(TEXT("stat unit")); Exec(TEXT("NiagaraDebugHud 1")); } },
		{ 60.0f, Say(TEXT("Scaling, live (rain.paint.Count): 100k drops, at the 120 Hz cap")) },
		{ 63.0f, Count(TEXT("rain.paint.Count 400000"), false) }, { 63.0f, Say(TEXT("Scaling, live (rain.paint.Count): 400k drops")) },
		{ 66.5f, Count(TEXT("rain.paint.Count 1600000"), false) },
		{ 66.5f, Say(TEXT("1.6M drops: the GPU sets the pace (about 50 ms a frame); the game thread stays under 3 ms")) },
		{ 73.0f, [this, Exec]() { Exec(TEXT("rain.paint.Count 100000")); Exec(TEXT("rain.paint.Balls 8000")); SetupRain(); SetupPaintBalls(); } },
		{ 73.0f, Play(3, true, 45.0f, 25.0f, 0.0f) },
		{ 73.0f, Say(TEXT("8,000 paint balls on the CPU: the game thread (stat unit: Game) climbs to about 7 ms")) },
		{ 78.0f, Count(TEXT("rain.paint.BallsGPU 1"), true) },
		{ 78.0f, Say(TEXT("The same HLSL on the GPU (rain.paint.BallsGPU 1): the game thread drops to about 1 ms")) },
		{ 83.0f, [this, Exec]() { Exec(TEXT("NiagaraDebugHud 0")); Exec(TEXT("stat unit")); Exec(TEXT("rain.paint.BallsGPU 0")); Exec(TEXT("rain.paint.Balls 3500"));
			Exec(TEXT("rain.paint.BallWait 30")); SetupPaintBalls(); } },
		{ 83.0f, Play(7, false, 210.0f, 30.0f, 14.0f) },
		{ 83.0f, Say(TEXT("Paint Valley  |  UE 5.7, C++, Niagara  |  two systems, one mechanic")) },
		{ 88.0f, [this]() { if (bQuitAfterDemo) { GEngine->Exec(GetWorld(), TEXT("quit")); } } },
	};
	DemoNext = 0;
	DemoClock = 0.0f;
}

void ARainPaintGame::TickDemo(float DeltaSeconds)
{
	if (DemoClock < 0.0f)
	{
		return;
	}
	DemoClock += DeltaSeconds;
	if (DemoSpin != 0.0f && Phase == EPaintPhase::Painting)
	{
		SetDirection(SteerAzimuth + DemoSpin * DeltaSeconds, SteerTilt);
	}
	while (DemoSteps.IsValidIndex(DemoNext) && DemoSteps[DemoNext].At <= DemoClock)
	{
		DemoSteps[DemoNext++].Do();
	}
	if (!DemoSteps.IsValidIndex(DemoNext))
	{
		DemoClock = -1.0f;
		DemoSpin = 0.0f;
		DemoCaption.Reset();
	}
}
