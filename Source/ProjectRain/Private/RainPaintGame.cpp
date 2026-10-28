#include "RainPaintGame.h"

#include "RainShieldTypes.h"
#include "DrawDebugHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

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
	3000,
	TEXT("Drops in the paint rain (Paint Valley, System 1). CarrierCount of them are big drops (they paint puddles); the rest are small drops (they make the puddles they land on spread)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPaintSmallPaints(
	TEXT("rain.paint.SmallPaints"),
	0,
	TEXT("0: small drops make the puddle they land on spread. 1: every small drop paints a small puddle of its own (for comparing the cost)."),
	ECVF_Default);

/** Small drops read back per second at most (the rest are sampled: each read-back hit counts for 1 / probability). */
static constexpr float SmallExportBudget = 6000.0f;

namespace
{
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
		Blocks.SplatRadius = 9.0f;
		Blocks.ParSeconds = 35.0f;
		Blocks.Ink = Hex(TEXT("6ab4ec"));
		// Blockers in Monument Valley 2's lilac, apart from the blue paint.
		Blocks.Palettes[Teal] = Palette(TEXT("Lilac"), TEXT("dcc6ec"), TEXT("bd9ad8"), TEXT("9a79bd"), TEXT("775b98"), TEXT("e4dcee"), TEXT("5a4870"), TEXT("ecdcf6"), 0.5f);
		Levels.Add(Blocks);
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
		PC->SetInputMode(FInputModeGameAndUI());
	}
	SetupRain();
	StartLevel(0);

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
	Rain->Activate(true);
}

FVector ARainPaintGame::Direction() const
{
	const double A = FMath::DegreesToRadians(SteerAzimuth);
	const double T = FMath::DegreesToRadians(SteerTilt);
	return FVector(FMath::Sin(T) * FMath::Cos(A), FMath::Sin(T) * FMath::Sin(A), -FMath::Cos(T));
}

void ARainPaintGame::UploadRain()
{
	if (!Rain || !Levels.IsValidIndex(LevelIndex))
	{
		return;
	}
	const bool bOn = Phase == EPaintPhase::Painting;
	Rain->SetVariableVec3(TEXT("RainGameDir"), Direction());
	Rain->SetVariableVec3(TEXT("RainGameCenter"), StreamCenter);
	Rain->SetVariableFloat(TEXT("RainGameHalf"), StreamRadius);
	Rain->SetVariableFloat(TEXT("RainGameLength"), 2.0f * StreamRadius);
	Rain->SetVariableFloat(TEXT("RainGameActive"), bOn ? 1.0f : 0.0f);
	Rain->SetVariableFloat(TEXT("RainGameScoring"), bOn ? 1.0f : 0.0f);
	Rain->SetVariableFloat(TEXT("RainPaintSpeed"), RainSpeed);
	Rain->SetVariableFloat(TEXT("RainPaintCarrierFrac"), RainCount > 0 ? FMath::Min(1.0f, float(CarrierCount) / RainCount) : 0.0f);
	// Every big drop's hit comes back; the small ones' are sampled to stay within the readback budget (a drop lands
	// about once per pass through the stream plus its ripple, ~1.3 s).
	const float SmallHitsPerSecond = FMath::Max(RainCount - CarrierCount, 1) / 1.3f;
	ExportProb = FMath::Min(1.0f, SmallExportBudget / SmallHitsPerSecond);
	Rain->SetVariableFloat(TEXT("RainPaintExportProb"), ExportProb);
	if (Camera)
	{
		Rain->SetVariableVec3(TEXT("RainViewFacing"), -Camera->GetActorForwardVector());
	}
	Rain->SetVariableLinearColor(TEXT("RainPaintInk"), FLinearColor::FromSRGBColor(Levels[LevelIndex].Ink));
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
	Clock = 0.0f;
	UploadRain();
}

void ARainPaintGame::ResetRound()
{
	Phase = EPaintPhase::Ready;
	Clock = 0.0f;
	PaintHits = 0;
	SmallHits = 0;
	SmallFed = 0;
	MaxHitsPerFrame = 0;
	Surface.ClearPaint();
	Surface.Flush();
	UploadRain();
}

void ARainPaintGame::SetDirection(float Azimuth, float Tilt)
{
	SteerAzimuth = FMath::UnwindDegrees(Azimuth);
	SteerTilt = FMath::Clamp(Tilt, 0.0f, 180.0f);
	UploadRain();
}

void ARainPaintGame::ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data, UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset)
{
	if (Phase != EPaintPhase::Painting || !Levels.IsValidIndex(LevelIndex))
	{
		return;
	}
	MaxHitsPerFrame = FMath::Max(MaxHitsPerFrame, Data.Num());
	const float Radius = Levels[LevelIndex].SplatRadius;
	const FVector O = GetActorLocation();
	const bool bSmallPaints = CVarPaintSmallPaints.GetValueOnGameThread() != 0;
	for (const FBasicParticleData& P : Data)
	{
		// Size carries the kind of box hit (+ 10 for a small drop); every kind takes paint, only the target's counts.
		const bool bSmall = P.Size > 9.5f;
		const FVector At = P.Position + SimulationPositionOffset - O;
		const FVector Dir = P.Velocity.IsNearlyZero() ? Direction() : P.Velocity.GetSafeNormal();
		const uint32 Seed = SplatSeed++;
		const float Jitter = 0.85f + 0.3f * float((Seed * 2654435761u) >> 8) / 16777216.0f;
		if (!bSmall)
		{
			Surface.AddPuddle(At, Radius * Jitter, Dir);
			++PaintHits;
		}
		else if (bSmallPaints)
		{
			Surface.AddPuddle(At, SmallPuddleRadius * Jitter, Dir);
			++SmallHits;
		}
		else
		{
			++SmallHits;
			SmallFed += Surface.Feed(At, 1.0f / FMath::Max(ExportProb, 1e-3f)) ? 1 : 0;
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
		const FString Line = FString::Printf(TEXT("PAINTMEASURE level=%d frames=%d frame_ms=%.2f game_ms=%.2f render_ms=%.2f gpu_ms=%.2f puddles=%d coverage=%.3f"),
			LevelIndex, MeasureFrames, MeasureFrameMs / N, MeasureGameMs / N, MeasureRenderMs / N, MeasureGpuMs / N, Surface.NumPuddles(), GetCoverage());
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
	if (PreviousAntiAliasing >= 0)
	{
		if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
		{
			AA->Set(PreviousAntiAliasing, ECVF_SetByCode);
		}
	}
	Super::EndPlay(Reason);
}

void ARainPaintGame::StartLevel(int32 Index)
{
	if (Levels.Num() == 0)
	{
		return;
	}
	LevelIndex = (Index % Levels.Num() + Levels.Num()) % Levels.Num();
	ViewHeight = Levels[LevelIndex].ViewHeight;
	BuildLevel();
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
	const TArray<FPaintBox> Collide = Surface.CollisionBoxes();
	BoxesUsed = Collide.Num();
	if (BoxesUsed > RainShield::MaxBoxes)
	{
		UE_LOG(LogTemp, Warning, TEXT("RainPaintGame: level %s has %d collision boxes, the rain tests only %d"), *L.Name, BoxesUsed, RainShield::MaxBoxes);
	}
	if (Rain)
	{
		for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
		{
			const bool bUsed = Collide.IsValidIndex(i);
			const float Kind = !bUsed ? RainShield::BoxNone : Collide[i].Kind == EPaintBoxKind::Target ? RainShield::BoxProtected
				: Collide[i].Kind == EPaintBoxKind::Blocker ? RainShield::BoxBlocker : KindGround;
			Rain->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMin"), i), bUsed ? O + Collide[i].Min : FVector(1e6));
			Rain->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMax"), i), bUsed ? O + Collide[i].Max : FVector(-1e6));
			Rain->SetVariableFloat(*FString::Printf(TEXT("RainBox%dKind"), i), Kind);
		}
	}
	Phase = EPaintPhase::Ready;
	Clock = 0.0f;
	PaintHits = 0;
	UploadRain();

	// One material per palette for the faces (with the atlas) and one for the spires (without).
	Materials.Reset();
	auto Lin = [](const FColor& C) { return FLinearColor::FromSRGBColor(C); };
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 p = 0; p < L.Palettes.Num(); ++p)
		{
			const FPaintPalette& P = L.Palettes[p];
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
			M->SetTextureParameterValue(TEXT("Atlas"), Atlas);
			M->SetScalarParameterValue(TEXT("UseAtlas"), Pass == 0 ? 1.0f : 0.0f);
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
			Surfaces->SetMaterial(Pass * L.Palettes.Num() + p, M);
			Materials.Add(M);
		}
	}
	if (BackdropMID)
	{
		BackdropMID->SetVectorParameterValue(TEXT("Center"), Lin(L.BackCenter));
		BackdropMID->SetVectorParameterValue(TEXT("Edge"), Lin(L.BackEdge));
		BackdropMID->SetVectorParameterValue(TEXT("Glow"), Lin(L.BackGlow));
		BackdropMID->SetVectorParameterValue(TEXT("GlowPos"), FLinearColor(L.BackGlowPos.X, L.BackGlowPos.Y, L.BackGlowPos.Z, L.BackGlowPos.W));
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
	// The island's shadow on the backdrop: where its centre lands in the view (orthographic: along the view), in
	// view widths from the middle; the shader applies the aspect ratio.
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
	// Key direction: the camera's right, so the face on the right of the view is the light one.
	if (Collection && GetWorld())
	{
		if (UMaterialParameterCollectionInstance* MPC = GetWorld()->GetParameterCollectionInstance(Collection))
		{
			const FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
			MPC->SetVectorParameterValue(TEXT("Key"), FLinearColor(Right.X, Right.Y, 0.0f, 0.0f));
		}
	}
}

void ARainPaintGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HandleInput(DeltaSeconds);
	TickMeasure(DeltaSeconds);
	if (Rain && CVarPaintRainCount.GetValueOnGameThread() != RainCount)
	{
		SetupRain();
	}
	if (Phase == EPaintPhase::Painting)
	{
		Clock += DeltaSeconds;
		if (GetCoverage() >= CoverageGoal && MeasureClock < 0.0f)
		{
			Phase = EPaintPhase::Done;
		}
	}
	Surface.Tick(DeltaSeconds);
	Surface.Flush();
	UploadRain();
	ShowHud();
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

	// Steering, any time: A/D (or left/right) turn the rain around the vertical, W/S (or up/down) tilt it.
	auto Axis = [PC](const FKey& Plus, const FKey& Plus2, const FKey& Minus, const FKey& Minus2)
	{
		return (PC->IsInputKeyDown(Plus) || PC->IsInputKeyDown(Plus2) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(Minus) || PC->IsInputKeyDown(Minus2) ? 1.0f : 0.0f);
	};
	SteerAzimuth = FMath::UnwindDegrees(SteerAzimuth + SteerYawRate * DeltaSeconds * Axis(EKeys::D, EKeys::Right, EKeys::A, EKeys::Left));
	SteerTilt = FMath::Clamp(SteerTilt + SteerTiltRate * DeltaSeconds * Axis(EKeys::W, EKeys::Up, EKeys::S, EKeys::Down), 0.0f, 180.0f);
	if (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar))
	{
		if (Phase == EPaintPhase::Done && Pressed(EKeys::Enter))
		{
			StartLevel(LevelIndex + 1);
		}
		else
		{
			StartRound();
		}
	}
	if (Pressed(EKeys::R)) ResetRound();
}

void ARainPaintGame::ShowHud() const
{
	if (!GEngine || !Levels.IsValidIndex(LevelIndex))
	{
		return;
	}
	const FPaintLevel& L = Levels[LevelIndex];
	const FColor Ink = L.Ink;
	// The rain's direction: an arrow coming in from upwind.
	const FVector D = Direction();
	DrawDebugDirectionalArrow(GetWorld(), StreamCenter - D * (0.95f * StreamRadius), StreamCenter - D * (0.6f * StreamRadius), 30.0f, Ink, false, -1.0f, SDPG_Foreground, 4.0f);
	const int32 Stars = Clock <= L.ParSeconds ? 3 : Clock <= 1.5f * L.ParSeconds ? 2 : 1;
	FString Line;
	switch (Phase)
	{
	case EPaintPhase::Ready:
		Line = FString::Printf(TEXT("%s  -  aim with WASD / arrows, Enter: rain  (goal %.0f%%, par %.0f s)"), *L.Name, 100.0f * CoverageGoal, L.ParSeconds);
		break;
	case EPaintPhase::Painting:
		Line = FString::Printf(TEXT("%s  -  %.1f s   painted %.1f%% / %.0f%%"), *L.Name, Clock, 100.0f * GetCoverage(), 100.0f * CoverageGoal);
		break;
	case EPaintPhase::Done:
		Line = FString::Printf(TEXT("%s  -  done in %.1f s  %s   (R: again, Enter: next level)"), *L.Name, Clock, Stars == 3 ? TEXT("***") : Stars == 2 ? TEXT("**") : TEXT("*"));
		break;
	}
	GEngine->AddOnScreenDebugMessage(9100, 0.0f, FColor(60, 40, 60), Line, false, FVector2D(1.4, 1.4));
	GEngine->AddOnScreenDebugMessage(9101, 0.0f, FColor(90, 80, 90), FString::Printf(TEXT("rain from azimuth %.0f, tilt %.0f    Q/E: turn the view    PgUp/PgDn: level"), SteerAzimuth, SteerTilt));
}

FString ARainPaintGame::Describe() const
{
	// Boxes in the particles' collision set (the GPU tests at most RainShield::MaxBoxes): the level's own plus 3 per arch.
	int32 Collide = 0;
	if (Levels.IsValidIndex(LevelIndex))
	{
		for (const FPaintBox& B : Levels[LevelIndex].Boxes)
		{
			Collide += B.bCollide ? 1 : 0;
		}
		Collide += 3 * Levels[LevelIndex].Arches.Num();
	}
	return FString::Printf(TEXT("level=%d faces=%d texels=%d visible=%d atlas=%dx%d build=%.3fs yaw=%.1f height=%.0f aspect=%.3f collide=%d target=%.0fcm2 phase=%d clock=%.2f coverage=%.4f bighits=%d smallhits=%d fed=%d maxhits=%d drops=%d exportprob=%.3f puddles=%d growing=%d"),
		LevelIndex, Surface.NumFaces(), Surface.NumTexels(), Surface.NumVisibleTexels(), Surface.GetAtlasWidth(), Surface.GetAtlasHeight(), BuildSeconds, ViewYaw, ViewHeight, ViewAspect,
		Collide, Surface.TargetAreaCm2(), int32(Phase), Clock, GetCoverage(), PaintHits, SmallHits, SmallFed, MaxHitsPerFrame, RainCount, ExportProb, Surface.NumPuddles(), Surface.NumGrowing());
}
