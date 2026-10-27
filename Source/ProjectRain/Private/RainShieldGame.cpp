#include "RainShieldGame.h"

#include "RainMagicController.h"
#include "RainShieldTypes.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Particles/ParticlePerfStatsManager.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------------------------------------- console
// Graders reproduce the performance numbers with these (see README).

static TAutoConsoleVariable<int32> CVarRainGameCount(
	TEXT("rain.game.Count"),
	RainShield::DefaultRainCount,
	TEXT("Rain Shield: drops in the game's rain (System 1, GPU sprites). Changing it reinitialises the rain system."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRainGameHailCount(
	TEXT("rain.game.HailCount"),
	RainShield::DefaultHailCount,
	TEXT("Rain Shield: hailstones (System 2, meshes). Changing it reinitialises the hail system."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRainGameMode(
	TEXT("rain.game.Mode"),
	0,
	TEXT("Rain Shield: 0 rain (System 1, GPU sprites), 1 hail (System 2, meshes). Switches while planning (key G)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRainGameHailGPU(
	TEXT("rain.game.HailGPU"),
	0,
	TEXT("Rain Shield: 0 simulates the hail on the CPU (NS_RainHail), 1 on the GPU (NS_RainHailGPU, the same HLSL): the CPU -> GPU port."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRainGamePauseUnselected(
	TEXT("rain.game.PauseUnselected"),
	1,
	TEXT("Rain Shield: 1 pauses and hides the weather system that is not selected (the measured optimisation); 0 keeps both\n")
	TEXT("simulating and drawing their parked, invisible particles (the 'before')."),
	ECVF_Default);

static FAutoConsoleCommandWithWorldAndArgs GRainGameSweep(
	TEXT("rain.game.Sweep"),
	TEXT("Rain Shield: steps the rain (or, with 'hail', the hail) through particle counts and writes frame / game thread / render thread / GPU / Niagara times to Saved/Sweeps/*.csv.\n")
	TEXT("Args: [hail] counts (default rain 0 50000 100000 200000 400000 800000 1600000, hail 0 500 1000 2000 5000 10000 20000 40000), optional settle=<s> measure=<s>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		TArray<int32> Counts;
		bool bHail = false;
		float Settle = 3.0f;
		float Measure = 5.0f;
		for (const FString& Arg : Args)
		{
			if (Arg == TEXT("hail"))
			{
				bHail = true;
			}
			else if (Arg.StartsWith(TEXT("settle=")))
			{
				Settle = FCString::Atof(*Arg.RightChop(7));
			}
			else if (Arg.StartsWith(TEXT("measure=")))
			{
				Measure = FCString::Atof(*Arg.RightChop(8));
			}
			else
			{
				Counts.Add(FCString::Atoi(*Arg));
			}
		}
		if (Counts.Num() == 0)
		{
			Counts = bHail ? TArray<int32>{ 0, 500, 1000, 2000, 5000, 10000, 20000, 40000 } : TArray<int32>{ 0, 50000, 100000, 200000, 400000, 800000, 1600000 };
		}
		for (TActorIterator<ARainShieldGame> It(World); It; ++It)
		{
			It->StartSweep(Counts, bHail, Settle, Measure);
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("rain.game.Sweep: no RainShieldGame in this world (open L_RainShield)"));
	}));

// ---------------------------------------------------------------------------------------------- levels

namespace
{
	TArray<FIntVector> Box(FIntVector Min, FIntVector Size)
	{
		TArray<FIntVector> Cells;
		for (int32 Z = 0; Z < Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					Cells.Add(Min + FIntVector(X, Y, Z));
				}
			}
		}
		return Cells;
	}

	FRainShieldPiece Piece(const TCHAR* Name, TArray<FIntVector> Cells)
	{
		FRainShieldPiece P;
		P.Name = Name;
		P.Cells = MoveTemp(Cells);
		return P;
	}

	TArray<FRainShieldLevel> DefaultLevels()
	{
		TArray<FRainShieldLevel> Levels;
		{
			FRainShieldLevel L;
			L.Name = TEXT("Cube");
			L.Protected = Box({ 4, 4, 4 }, { 2, 2, 2 });
			L.Pieces = { Piece(TEXT("Plate 4x4"), Box({ 0, 0, 0 }, { 4, 4, 1 })) };
			L.Duration = 15.0f;
			Levels.Add(L);
		}
		{
			// A lamp post: a column, an arm and the lamp hanging from it. Its outline is small from above and
			// large from the side, and two small pieces only cover it from the right direction.
			FRainShieldLevel L;
			L.Name = TEXT("Lamp post");
			L.Protected = Box({ 4, 5, 1 }, { 1, 1, 6 });
			L.Protected.Append({ { 5, 5, 6 }, { 6, 5, 6 }, { 6, 5, 5 } });
			L.Pieces = { Piece(TEXT("Bar 1x4"), Box({ 0, 0, 0 }, { 4, 1, 1 })), Piece(TEXT("Plate 2x2"), Box({ 0, 0, 0 }, { 2, 2, 1 })) };
			L.Duration = 20.0f;
			Levels.Add(L);
		}
		{
			FRainShieldLevel L;
			L.Name = TEXT("Arch");
			L.Protected = Box({ 3, 5, 1 }, { 1, 1, 4 });
			L.Protected.Append(Box({ 6, 5, 1 }, { 1, 1, 4 }));
			L.Protected.Append(Box({ 3, 5, 5 }, { 4, 1, 1 }));
			L.Pieces = {
				Piece(TEXT("Plate 3x3"), Box({ 0, 0, 0 }, { 3, 3, 1 })),
				Piece(TEXT("L plate"), { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 0, 1, 0 } }),
				Piece(TEXT("Block 2x2x2"), Box({ 0, 0, 0 }, { 2, 2, 2 })),
			};
			L.Duration = 25.0f;
			Levels.Add(L);
		}
		return Levels;
	}

	// Merges grid cells into few axis-aligned boxes (greedy: runs along X, then rows along Y, then slabs along Z).
	// Returns [Min, Max) cell ranges.
	TArray<TPair<FIntVector, FIntVector>> MergeCells(const TSet<FIntVector>& Cells)
	{
		TSet<FIntVector> Left = Cells;
		TArray<FIntVector> Sorted = Cells.Array();
		Sorted.Sort([](const FIntVector& A, const FIntVector& B)
		{
			return A.Z != B.Z ? A.Z < B.Z : (A.Y != B.Y ? A.Y < B.Y : A.X < B.X);
		});
		TArray<TPair<FIntVector, FIntVector>> Boxes;
		for (const FIntVector& C : Sorted)
		{
			if (!Left.Contains(C))
			{
				continue;
			}
			FIntVector Max = C + FIntVector(1, 1, 1);
			while (Left.Contains(FIntVector(Max.X, C.Y, C.Z)))
			{
				++Max.X;
			}
			auto RowFull = [&](int32 Y, int32 Z)
			{
				for (int32 X = C.X; X < Max.X; ++X)
				{
					if (!Left.Contains(FIntVector(X, Y, Z)))
					{
						return false;
					}
				}
				return true;
			};
			while (RowFull(Max.Y, C.Z))
			{
				++Max.Y;
			}
			auto SlabFull = [&](int32 Z)
			{
				for (int32 Y = C.Y; Y < Max.Y; ++Y)
				{
					if (!RowFull(Y, Z))
					{
						return false;
					}
				}
				return true;
			};
			while (SlabFull(Max.Z))
			{
				++Max.Z;
			}
			for (int32 Z = C.Z; Z < Max.Z; ++Z)
			{
				for (int32 Y = C.Y; Y < Max.Y; ++Y)
				{
					for (int32 X = C.X; X < Max.X; ++X)
					{
						Left.Remove(FIntVector(X, Y, Z));
					}
				}
			}
			Boxes.Add({ C, Max });
		}
		return Boxes;
	}

	// A quarter turn of a cell about a grid axis, then shifted so the piece starts at 0 again.
	TArray<FIntVector> Rotate(const TArray<FIntVector>& Cells, int32 Axis)
	{
		TArray<FIntVector> Out;
		FIntVector Min(INT_MAX);
		for (const FIntVector& C : Cells)
		{
			const FIntVector R = Axis == 0 ? FIntVector(C.X, -C.Z, C.Y) : (Axis == 1 ? FIntVector(C.Z, C.Y, -C.X) : FIntVector(-C.Y, C.X, C.Z));
			Out.Add(R);
			Min = FIntVector(FMath::Min(Min.X, R.X), FMath::Min(Min.Y, R.Y), FMath::Min(Min.Z, R.Z));
		}
		for (FIntVector& C : Out)
		{
			C -= Min;
		}
		return Out;
	}

	FIntVector Extent(const TArray<FIntVector>& Cells)
	{
		FIntVector Max(0);
		for (const FIntVector& C : Cells)
		{
			Max = FIntVector(FMath::Max(Max.X, C.X + 1), FMath::Max(Max.Y, C.Y + 1), FMath::Max(Max.Z, C.Z + 1));
		}
		return Max;
	}
}

// ---------------------------------------------------------------------------------------------- setup

ARainShieldGame::ARainShieldGame()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the camera manager: the orbit camera placed here is the one the rain controller reads next frame.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Rain = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Rain"));
	Rain->SetupAttachment(RootComponent);
	Rain->bAutoActivate = false;
	Hail = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Hail"));
	Hail->SetupAttachment(RootComponent);
	Hail->bAutoActivate = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(TEXT("/Game/RainMagic/Materials/M_RainBasic.M_RainBasic"));
	CubeMaterial = BasicMaterial.Object;
	for (TObjectPtr<UInstancedStaticMeshComponent>* Cubes : { &ProtectedCubes, &BlockerCubes })
	{
		*Cubes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Cubes == &ProtectedCubes ? TEXT("ProtectedCubes") : TEXT("BlockerCubes"));
		(*Cubes)->SetupAttachment(RootComponent);
		(*Cubes)->SetStaticMesh(CubeMesh.Object);
		(*Cubes)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Lit by the stage's key light too (channel 1, the performer's), not only the dim ground light.
		(*Cubes)->LightingChannels.bChannel1 = true;
	}
	RainSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RainMagic/Niagara/NS_RainGame.NS_RainGame")));
	HailSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RainMagic/Niagara/NS_RainHail.NS_RainHail")));
	HailSystemGPU = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/RainMagic/Niagara/NS_RainHailGPU.NS_RainHailGPU")));
	Levels = DefaultLevels();

	// The 26 directions: the 6 axes first (falling down first), then the 12 edge and 8 corner diagonals.
	Directions = { { 0, 0, -1 }, { 0, 0, 1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
	for (int32 NonZero = 2; NonZero <= 3; ++NonZero)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 X = -1; X <= 1; ++X)
				{
					if ((X != 0) + (Y != 0) + (Z != 0) == NonZero)
					{
						Directions.Add(FIntVector(X, Y, Z));
					}
				}
			}
		}
	}
}

void ARainShieldGame::BeginPlay()
{
	Super::BeginPlay();
	if (!Controller)
	{
		for (TActorIterator<ARainMagicController> It(GetWorld()); It; ++It)
		{
			Controller = *It;
			break;
		}
	}
	if (Controller)
	{
		// The film's trick hands the scene over: no film rain, no performer, no trick keys.
		Controller->bGameMode = true;
		Controller->ExtraRainComponents.AddUnique(Rain);
		if (Controller->RainActor)
		{
			Controller->RainActor->GetNiagaraComponent()->DeactivateImmediate();
			Controller->RainActor->SetActorHiddenInGame(true);
		}
		if (Controller->Performer)
		{
			Controller->Performer->SetActorHiddenInGame(true);
		}
	}

	if (CubeMaterial)
	{
		ProtectedMaterial = UMaterialInstanceDynamic::Create(CubeMaterial, this);
		BlockerMaterial = UMaterialInstanceDynamic::Create(CubeMaterial, this);
		BlockerMaterial->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.10f, 0.11f, 0.13f));
		BlockerMaterial->SetScalarParameterValue(TEXT("Metallic"), 0.4f);
		BlockerMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.35f);
		BlockerMaterial->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.004f, 0.006f, 0.010f));
		ProtectedCubes->SetMaterial(0, ProtectedMaterial);
		BlockerCubes->SetMaterial(0, BlockerMaterial);
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Camera = GetWorld()->SpawnActor<ACineCameraActor>(GridCenter(), FRotator::ZeroRotator, Params);
	if (UCineCameraComponent* Cine = Camera ? Camera->GetCineCameraComponent() : nullptr)
	{
		Cine->bConstrainAspectRatio = false;
		Cine->SetCurrentFocalLength(FocalLength);
		Cine->CurrentAperture = FStop;
		Cine->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
		// Physical camera exposure: f/8 lets in (8 / 2.2)^2 less light than the film lenses the stage is lit for.
		Cine->PostProcessSettings.bOverride_AutoExposureBias = true;
		Cine->PostProcessSettings.AutoExposureBias = ExposureBias + 2.0f * FMath::Log2(FStop / FMath::Max(ReferenceFStop, 0.5f));
	}
	Backlight = GetWorld()->SpawnActor<APointLight>(GridCenter(), FRotator::ZeroRotator, Params);
	if (UPointLightComponent* Lamp = Backlight ? Cast<UPointLightComponent>(Backlight->GetLightComponent()) : nullptr)
	{
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetIntensity(BacklightIntensity);
		Lamp->SetSourceRadius(40.0f);
		Lamp->SetCastShadows(false);
		Lamp->SetLightingChannels(false, true, false);
		if (Controller)
		{
			// The performer's key light (first glint lamp) has nothing to light in the game.
			if (Controller->GlintLights.Num() > 0)
			{
				Controller->GlintLights[0] = Backlight;
			}
			else
			{
				Controller->GlintLights.Add(Backlight);
			}
		}
	}

	// Mean fall speed of the drops (Atlas et al. 1973 over the uniform size range): the expected-hit estimate.
	double Sum = 0.0;
	const int32 Samples = 64;
	for (int32 i = 0; i < Samples; ++i)
	{
		const double D = RainShield::DropMinMm + (RainShield::DropMaxMm - RainShield::DropMinMm) * (i + 0.5) / Samples;
		Sum += FMath::Max(0.0, 9.65 - 10.3 * FMath::Exp(-0.6 * D)) * 100.0;
	}
	MeanFallSpeed = float(Sum / Samples);

	RainCount = FMath::Max(CVarRainGameCount.GetValueOnGameThread(), 0);
	if (FParse::Param(FCommandLine::Get(), TEXT("RainHailGPU")))
	{
		CVarRainGameHailGPU->Set(1);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RainNoPauseUnselected")))
	{
		CVarRainGamePauseUnselected->Set(0);
	}
	HailCount = FMath::Max(CVarRainGameHailCount.GetValueOnGameThread(), 0);
	bHailMode = CVarRainGameMode.GetValueOnGameThread() == 1;
	bHailGpu = CVarRainGameHailGPU.GetValueOnGameThread() == 1;
	SetupSystem(Rain, RainSystem.LoadSynchronous(), RainCount);
	// A 180-degree shutter at 60 fps: short streaks.
	Rain->SetVariableFloat(TEXT("RainShutter"), 0.5f / 60.0f);
	SetupSystem(Hail, (bHailGpu ? HailSystemGPU : HailSystem).LoadSynchronous(), HailCount);
	StartLevel(0);

	// Reproducible sweeps from the command line (standalone 1080p runs, see README):
	//   -RainSweep=0,50000,... (or -RainSweep=hail:0,500,...)  -RainSweepQuit  -RainSweepSettle=<s>  -RainSweepMeasure=<s>
	FString Sweep;
	if (FParse::Value(FCommandLine::Get(), TEXT("RainSweep="), Sweep, false))
	{
		bPendingSweepHail = Sweep.RemoveFromStart(TEXT("hail:"));
		TArray<FString> Parts;
		Sweep.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			PendingSweep.Add(FCString::Atoi(*Part));
		}
		bQuitAfterSweep = FParse::Param(FCommandLine::Get(), TEXT("RainSweepQuit"));
		PendingSweepDelay = 4.0f;
	}
}

void ARainShieldGame::SetupSystem(UNiagaraComponent* Component, UNiagaraSystem* System, int32 Count)
{
	if (!System)
	{
		UE_LOG(LogTemp, Error, TEXT("RainShieldGame: Niagara system missing for %s (run build_rain_game_assets / build_rain_hail_assets)"), *Component->GetName());
		return;
	}
	Component->SetAsset(System);
	Component->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
	Component->SetVariableObject(TEXT("RainHitHandler"), this);
	Component->SetVariableInt(TEXT("RainGameCount"), Count);
	Component->SetVariableFloat(TEXT("RainGameGust"), Gust);
	Component->Activate(true);
}

void ARainShieldGame::ApplyHailAsset()
{
	SetupSystem(Hail, (bHailGpu ? HailSystemGPU : HailSystem).LoadSynchronous(), HailCount);
	UploadBoxes(Phase != ERainShieldPhase::Plan);
	UploadRain();
}

void ARainShieldGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (SweepListener)
	{
		FParticlePerfStatsManager::RemoveListener(SweepListener.Get());
		SweepListener.Reset();
	}
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------- grid

FVector ARainShieldGame::GridCenter() const
{
	return GetActorLocation() + FVector(0.0, 0.0, GridHeight);
}

FVector ARainShieldGame::CellCenter(const FIntVector& Cell) const
{
	return GridCenter() + (FVector(Cell) + 0.5 - 0.5 * GridSize) * CellSize;
}

FVector ARainShieldGame::Direction() const
{
	return FVector(Directions[DirectionIndex]).GetSafeNormal();
}

FString ARainShieldGame::DirectionName(int32 Index) const
{
	const FIntVector D = Directions[Index];
	TArray<FString> Parts;
	if (D.Z != 0) Parts.Add(D.Z < 0 ? TEXT("DOWN") : TEXT("UP"));
	if (D.X != 0) Parts.Add(D.X > 0 ? TEXT("+X") : TEXT("-X"));
	if (D.Y != 0) Parts.Add(D.Y > 0 ? TEXT("+Y") : TEXT("-Y"));
	return FString::Join(Parts, TEXT(" "));
}

float ARainShieldGame::StreamHalf() const
{
	// Wide enough to cover the grid seen from any of the 26 directions (its half diagonal) plus the gusts.
	return 0.5f * GridSize * CellSize * UE_SQRT_3 + 30.0f;
}

float ARainShieldGame::StreamLength() const
{
	// The face sits outside the grid in every direction, plus the fade-in at either end.
	return GridSize * CellSize * UE_SQRT_3 + 180.0f;
}

TArray<FIntVector> ARainShieldGame::GhostCells() const
{
	TArray<FIntVector> Cells;
	for (const FIntVector& C : PieceCells)
	{
		Cells.Add(PieceAnchor + C);
	}
	return Cells;
}

bool ARainShieldGame::IsFree(const TArray<FIntVector>& Cells) const
{
	for (const FIntVector& C : Cells)
	{
		if (C.X < 0 || C.Y < 0 || C.Z < 0 || C.X >= GridSize || C.Y >= GridSize || C.Z >= GridSize || ProtectedSet.Contains(C))
		{
			return false;
		}
		for (const FPlaced& P : Placed)
		{
			if (P.Cells.Contains(C))
			{
				return false;
			}
		}
	}
	return true;
}

void ARainShieldGame::ResetPiece(int32 Index)
{
	const TArray<FRainShieldPiece>& Pieces = Levels[LevelIndex].Pieces;
	CurrentPiece = Index;
	PieceCells = Pieces.IsValidIndex(Index) ? Pieces[Index].Cells : TArray<FIntVector>();
	// Start above the middle of the grid.
	const FIntVector E = Extent(PieceCells);
	PieceAnchor = FIntVector((GridSize - E.X) / 2, (GridSize - E.Y) / 2, GridSize - E.Z);
}

void ARainShieldGame::UploadBoxes(bool bWithBlockers)
{
	TArray<TPair<FBox, float>> Boxes;
	const FVector Min = GridCenter() - FVector(0.5 * GridSize * CellSize);
	auto Add = [&](const TSet<FIntVector>& Cells, float Kind)
	{
		for (const TPair<FIntVector, FIntVector>& B : MergeCells(Cells))
		{
			Boxes.Add({ FBox(Min + FVector(B.Key) * CellSize, Min + FVector(B.Value) * CellSize), Kind });
		}
	};
	Add(ProtectedSet, RainShield::BoxProtected);
	if (bWithBlockers)
	{
		TSet<FIntVector> Blockers;
		for (const FPlaced& P : Placed)
		{
			Blockers.Append(P.Cells);
		}
		Add(Blockers, RainShield::BoxBlocker);
	}
	BoxesUsed = Boxes.Num();
	if (BoxesUsed > RainShield::MaxBoxes)
	{
		UE_LOG(LogTemp, Warning, TEXT("RainShieldGame: %d boxes, the rain tests only %d"), BoxesUsed, RainShield::MaxBoxes);
	}
	for (int32 i = 0; i < RainShield::MaxBoxes; ++i)
	{
		const bool bUsed = Boxes.IsValidIndex(i);
		const FVector Lo = bUsed ? Boxes[i].Key.Min : FVector(1e6);
		const FVector Hi = bUsed ? Boxes[i].Key.Max : FVector(-1e6);
		for (UNiagaraComponent* System : { Rain.Get(), Hail.Get() })
		{
			System->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMin"), i), Lo);
			System->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMax"), i), Hi);
			System->SetVariableFloat(*FString::Printf(TEXT("RainBox%dKind"), i), bUsed ? Boxes[i].Value : RainShield::BoxNone);
		}
	}
}

void ARainShieldGame::UploadRain()
{
	// Both systems share the stream; only the selected one falls. The other is paused and hidden: parked particles
	// still cost simulation, sorting and draw submission (200k parked drops were ~4 ms of GPU at 1080p).
	for (UNiagaraComponent* System : { Rain.Get(), Hail.Get() })
	{
		const bool bSelected = System == ActiveSystem();
		const bool bRun = bSelected || CVarRainGamePauseUnselected.GetValueOnGameThread() == 0;
		System->SetPaused(!bRun);
		System->SetVisibility(bRun);
		System->SetVariableVec3(TEXT("RainGameDir"), Direction());
		System->SetVariableVec3(TEXT("RainGameCenter"), GridCenter());
		System->SetVariableFloat(TEXT("RainGameHalf"), StreamHalf());
		System->SetVariableFloat(TEXT("RainGameLength"), StreamLength());
		System->SetVariableFloat(TEXT("RainGameActive"), bSelected && Phase != ERainShieldPhase::Plan ? 1.0f : 0.0f);
		System->SetVariableFloat(TEXT("RainGameScoring"), bSelected && Phase == ERainShieldPhase::Raining ? 1.0f : 0.0f);
		System->SetVariableFloat(TEXT("RainGameGust"), Gust);
	}
}

void ARainShieldGame::RefreshCubes()
{
	const FVector Scale(CellSize / 100.0f * 0.98f);
	ProtectedCubes->ClearInstances();
	for (const FIntVector& C : ProtectedSet)
	{
		ProtectedCubes->AddInstance(FTransform(FRotator::ZeroRotator, CellCenter(C), Scale), true);
	}
	BlockerCubes->ClearInstances();
	for (const FPlaced& P : Placed)
	{
		for (const FIntVector& C : P.Cells)
		{
			BlockerCubes->AddInstance(FTransform(FRotator::ZeroRotator, CellCenter(C), Scale), true);
		}
	}
}

// ---------------------------------------------------------------------------------------------- game flow

void ARainShieldGame::StartLevel(int32 Index)
{
	if (Levels.Num() == 0)
	{
		return;
	}
	LevelIndex = (Index % Levels.Num() + Levels.Num()) % Levels.Num();
	const FRainShieldLevel& Level = Levels[LevelIndex];
	ProtectedSet = TSet<FIntVector>(Level.Protected);
	Placed.Reset();
	ResetPiece(0);
	Phase = ERainShieldPhase::Plan;
	RoundClock = 0.0f;
	Hits = 0;
	HitWaterMl = 0.0;
	BrokenBlockers = 0;
	BlockerImpacts = 0;
	RecentHits.Reset();

	// The reference outline: the shape's largest shadow along a grid axis (cells x cell area).
	ReferenceArea = 0.0f;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		TSet<FIntPoint> Shadow;
		for (const FIntVector& C : ProtectedSet)
		{
			Shadow.Add(Axis == 0 ? FIntPoint(C.Y, C.Z) : (Axis == 1 ? FIntPoint(C.X, C.Z) : FIntPoint(C.X, C.Y)));
		}
		ReferenceArea = FMath::Max(ReferenceArea, Shadow.Num() * CellSize * CellSize);
	}
	if (ProtectedMaterial)
	{
		ProtectedMaterial->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.75f, 0.52f, 0.28f));
		ProtectedMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.7f);
		ProtectedMaterial->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.030f, 0.018f, 0.008f));
	}
	RefreshCubes();
	UploadBoxes(false);
	UploadRain();
}

void ARainShieldGame::SelectNextPiece()
{
	const int32 Num = Levels[LevelIndex].Pieces.Num();
	for (int32 Step = 1; Step <= Num; ++Step)
	{
		const int32 Next = (CurrentPiece + Step) % Num;
		if (!Placed.ContainsByPredicate([Next](const FPlaced& P) { return P.Piece == Next; }))
		{
			ResetPiece(Next);
			return;
		}
	}
}

void ARainShieldGame::MovePiece(FIntVector Delta)
{
	if (Phase != ERainShieldPhase::Plan || PieceCells.Num() == 0)
	{
		return;
	}
	const FIntVector E = Extent(PieceCells);
	PieceAnchor += Delta;
	PieceAnchor = FIntVector(FMath::Clamp(PieceAnchor.X, 0, GridSize - E.X), FMath::Clamp(PieceAnchor.Y, 0, GridSize - E.Y), FMath::Clamp(PieceAnchor.Z, 0, GridSize - E.Z));
}

void ARainShieldGame::RotatePiece(int32 Axis)
{
	if (Phase != ERainShieldPhase::Plan || PieceCells.Num() == 0)
	{
		return;
	}
	PieceCells = Rotate(PieceCells, FMath::Clamp(Axis, 0, 2));
	MovePiece(FIntVector::ZeroValue);
}

bool ARainShieldGame::PlacePiece()
{
	if (Phase != ERainShieldPhase::Plan || PieceCells.Num() == 0)
	{
		return false;
	}
	const TArray<FIntVector> Cells = GhostCells();
	if (!IsFree(Cells))
	{
		return false;
	}
	Placed.Add({ CurrentPiece, Cells, 0, 0 });
	RefreshCubes();
	if (Placed.Num() < Levels[LevelIndex].Pieces.Num())
	{
		SelectNextPiece();
	}
	else
	{
		PieceCells.Reset();
	}
	return true;
}

void ARainShieldGame::UndoPiece()
{
	if (Phase != ERainShieldPhase::Plan || Placed.Num() == 0)
	{
		return;
	}
	const FPlaced Last = Placed.Pop();
	RefreshCubes();
	ResetPiece(Last.Piece);
}

void ARainShieldGame::SetDirection(int32 Index)
{
	if (Phase == ERainShieldPhase::Plan)
	{
		DirectionIndex = (Index % Directions.Num() + Directions.Num()) % Directions.Num();
	}
}

void ARainShieldGame::StartRound()
{
	if (Phase != ERainShieldPhase::Plan)
	{
		return;
	}
	Phase = ERainShieldPhase::Raining;
	RoundClock = 0.0f;
	Hits = 0;
	HitWaterMl = 0.0;
	MaxHitsPerCallback = 0;
	SaturatedCallbacks = 0;
	BlockerImpacts = 0;
	for (FPlaced& P : Placed)
	{
		P.HitPoints = P.MaxHitPoints = PieceHitPoints(P.Cells.Num());
	}
	UploadBoxes(true);
	UploadRain();
}

void ARainShieldGame::EndRound()
{
	// The rain keeps falling but stops scoring; hits still in flight (read back a few frames late) count.
	Phase = ERainShieldPhase::Result;
	UploadRain();
}

float ARainShieldGame::ExpectedHits(float Seconds) const
{
	// Drops per cm^3 x mean fall speed x the shape's reference outline x time: what an unshielded shape of that
	// outline catches. The first drops need (L/2 - grid/2) / v to reach the grid.
	const float H = StreamHalf();
	const float L = StreamLength();
	const float Density = (bHailMode ? HailCount : RainCount) / (4.0f * H * H * L);
	const float Speed = bHailMode ? RainShield::HailSpeed : MeanFallSpeed;
	const float Arrive = (0.5f * L - 0.5f * GridSize * CellSize) / FMath::Max(Speed, 1.0f);
	return Density * Speed * ReferenceArea * FMath::Max(Seconds - Arrive, 0.0f);
}

int32 ARainShieldGame::PieceHitPoints(int32 Cells) const
{
	// The hail's impacts on one exposed cell in BlockerLifeSeconds: stones per cm^3 x speed x cell area x time.
	const float H = StreamHalf();
	const float L = StreamLength();
	const float PerCell = HailCount / (4.0f * H * H * L) * RainShield::HailSpeed * CellSize * CellSize * BlockerLifeSeconds;
	return FMath::Max(FMath::RoundToInt(Cells * PerCell), 1);
}

float ARainShieldGame::GetWetness() const
{
	const float Expected = ExpectedHits(RoundClock);
	return Expected > 1.0f ? Hits / Expected : 0.0f;
}

int32 ARainShieldGame::GetStars() const
{
	const float W = GetWetness();
	return W <= StarWetness.X ? 3 : (W <= StarWetness.Y ? 2 : (W <= StarWetness.Z ? 1 : 0));
}

int32 ARainShieldGame::GetLiveParticleCount() const
{
	return LiveCount(ActiveSystem());
}

int32 ARainShieldGame::LiveCount(const UNiagaraComponent* Component)
{
	int32 Count = 0;
	if (FNiagaraSystemInstanceControllerConstPtr SIC = Component ? Component->GetSystemInstanceController() : nullptr)
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

void ARainShieldGame::SetHailMode(bool bHail)
{
	if (Phase != ERainShieldPhase::Plan && SweepIndex == INDEX_NONE)
	{
		return;
	}
	bHailMode = bHail;
	UploadRain();
}

void ARainShieldGame::SetHailCount(int32 Count)
{
	HailCount = FMath::Max(Count, 0);
	Hail->SetVariableInt(TEXT("RainGameCount"), HailCount);
	Hail->ReinitializeSystem();
	UploadBoxes(Phase != ERainShieldPhase::Plan);
	UploadRain();
}

void ARainShieldGame::SetRainCount(int32 Count)
{
	RainCount = FMath::Max(Count, 0);
	Rain->SetVariableInt(TEXT("RainGameCount"), RainCount);
	// A fresh instance: the burst fires once with the new count (ResetSystem would add drops to the old ones).
	Rain->ReinitializeSystem();
	UploadBoxes(Phase != ERainShieldPhase::Plan);
	UploadRain();
}

void ARainShieldGame::ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data, UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset)
{
	if (Hail && NiagaraSystem == Hail->GetAsset())
	{
		OnHailImpacts(Data, SimulationPositionOffset);
		return;
	}
	Hits += Data.Num();
	MaxHitsPerCallback = FMath::Max(MaxHitsPerCallback, Data.Num());
	if (Data.Num() >= RainShield::ExportCapacity)
	{
		// The export buffer was full: some hits of that frame were dropped.
		++SaturatedCallbacks;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	for (const FBasicParticleData& P : Data)
	{
		// Size is the drop's diameter (cm): its volume in ml.
		HitWaterMl += UE_PI / 6.0 * P.Size * P.Size * P.Size;
		if (RecentHits.Num() < 400)
		{
			RecentHits.Add({ P.Position + SimulationPositionOffset, Now });
		}
	}
}

void ARainShieldGame::OnHailImpacts(const TArray<FBasicParticleData>& Data, const FVector& Offset)
{
	// Velocity carries (box kind, impact speed cm/s, mass g); Size the stone's radius (HailUpdate.hlsl).
	MaxHitsPerCallback = FMath::Max(MaxHitsPerCallback, Data.Num());
	const float Now = GetWorld()->GetTimeSeconds();
	bool bBroke = false;
	for (const FBasicParticleData& P : Data)
	{
		const FVector At = P.Position + Offset;
		if (P.Velocity.X > 1.5f)
		{
			// The shape: a hit, like a drop (meltwater of the stone's mass, in ml).
			++Hits;
			HitWaterMl += P.Velocity.Z;
			if (RecentHits.Num() < 400)
			{
				RecentHits.Add({ At, Now });
			}
			continue;
		}
		// A blocker: the piece whose cell is nearest the impact loses a hit point; at zero it breaks.
		++BlockerImpacts;
		int32 Best = INDEX_NONE;
		double BestDist = UE_BIG_NUMBER;
		for (int32 i = 0; i < Placed.Num(); ++i)
		{
			for (const FIntVector& C : Placed[i].Cells)
			{
				const double Dist = FVector::DistSquared(CellCenter(C), At);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Best = i;
				}
			}
		}
		if (Best != INDEX_NONE && --Placed[Best].HitPoints <= 0)
		{
			Flash = FString::Printf(TEXT("%s broke!"), *Levels[LevelIndex].Pieces[Placed[Best].Piece].Name);
			FlashTime = 2.5f;
			Placed.RemoveAt(Best);
			++BrokenBlockers;
			bBroke = true;
		}
	}
	if (bBroke)
	{
		RefreshCubes();
		UploadBoxes(true);
	}
}

// ---------------------------------------------------------------------------------------------- tick

void ARainShieldGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	// The film rain hands over to the game: kept off. Its actor auto-activates in its own BeginPlay, which can come
	// after ours (the Niagara debug HUD showed its 245k drops still simulating, hidden, in every early measurement).
	if (Controller && Controller->RainActor)
	{
		UNiagaraComponent* FilmRain = Controller->RainActor->GetNiagaraComponent();
		if (FilmRain && FilmRain->IsActive())
		{
			FilmRain->SetAutoActivate(false);
			FilmRain->DeactivateImmediate();
		}
	}
	if (!bViewSet && PC && Camera)
	{
		PC->SetViewTarget(Camera);
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeGameAndUI());
		bViewSet = true;
	}

	if (SweepIndex == INDEX_NONE)
	{
		const int32 WantedRain = FMath::Max(CVarRainGameCount.GetValueOnGameThread(), 0);
		if (WantedRain != RainCount)
		{
			SetRainCount(WantedRain);
		}
		const int32 WantedHail = FMath::Max(CVarRainGameHailCount.GetValueOnGameThread(), 0);
		if (WantedHail != HailCount)
		{
			SetHailCount(WantedHail);
		}
		const bool bWantGpu = CVarRainGameHailGPU.GetValueOnGameThread() == 1;
		if (bWantGpu != bHailGpu)
		{
			bHailGpu = bWantGpu;
			ApplyHailAsset();
		}
		static int32 LastPause = 1;
		if (CVarRainGamePauseUnselected.GetValueOnGameThread() != LastPause)
		{
			LastPause = CVarRainGamePauseUnselected.GetValueOnGameThread();
			UploadRain();
		}
		const bool bWantHail = CVarRainGameMode.GetValueOnGameThread() == 1;
		static bool bLastWantHail = false;
		if (bWantHail != bLastWantHail)
		{
			bLastWantHail = bWantHail;
			SetHailMode(bWantHail);
		}
	}
	if (PendingSweepDelay > 0.0f)
	{
		PendingSweepDelay -= float(FApp::GetDeltaTime());
		if (PendingSweepDelay <= 0.0f)
		{
			float Settle = 3.0f, Measure = 5.0f;
			FParse::Value(FCommandLine::Get(), TEXT("RainSweepSettle="), Settle);
			FParse::Value(FCommandLine::Get(), TEXT("RainSweepMeasure="), Measure);
			StartSweep(PendingSweep, bPendingSweepHail, Settle, Measure);
		}
	}
	FlashTime -= DeltaSeconds;

	HandleInput(DeltaSeconds);
	UpdateCamera();

	if (Phase == ERainShieldPhase::Raining && SweepIndex == INDEX_NONE)
	{
		RoundClock += DeltaSeconds;
		if (RoundClock >= Levels[LevelIndex].Duration)
		{
			RoundClock = Levels[LevelIndex].Duration;
			EndRound();
		}
	}
	if (ProtectedMaterial && Phase != ERainShieldPhase::Plan)
	{
		// The shape darkens and turns glossy as it gets wet.
		const float Wet = FMath::Clamp(GetWetness() / StarWetness.Z, 0.0f, 1.0f);
		ProtectedMaterial->SetVectorParameterValue(TEXT("BaseColor"), FMath::Lerp(FLinearColor(0.75f, 0.52f, 0.28f), FLinearColor(0.12f, 0.16f, 0.24f), Wet));
		ProtectedMaterial->SetVectorParameterValue(TEXT("Emissive"), FMath::Lerp(FLinearColor(0.030f, 0.018f, 0.008f), FLinearColor(0.004f, 0.008f, 0.016f), Wet));
		ProtectedMaterial->SetScalarParameterValue(TEXT("Roughness"), FMath::Lerp(0.7f, 0.08f, Wet));
	}

	const float Now = GetWorld()->GetTimeSeconds();
	RecentHits.RemoveAll([Now](const TPair<FVector, float>& H) { return Now - H.Value > 0.4f; });
	for (const TPair<FVector, float>& H : RecentHits)
	{
		DrawDebugPoint(GetWorld(), H.Key, 5.0f, FColor(120, 200, 255), false, -1.0f, SDPG_World);
	}
	DrawPlan();
	TickSweep(DeltaSeconds);
	ShowHud();
}

void ARainShieldGame::HandleInput(float DeltaSeconds)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	auto Pressed = [PC](const FKey& Key) { return PC->WasInputKeyJustPressed(Key); };

	// Orbit: right mouse drag or the arrow keys; zoom: the wheel.
	float MX = 0.0f, MY = 0.0f;
	const bool bHaveMouse = PC->GetMousePosition(MX, MY);
	if (PC->IsInputKeyDown(EKeys::RightMouseButton) && bHaveMouse)
	{
		if (bOrbiting)
		{
			OrbitYaw += (MX - LastMouse.X) * 0.3f;
			OrbitPitch = FMath::Clamp(OrbitPitch - (MY - LastMouse.Y) * 0.3f, -85.0f, 85.0f);
		}
		bOrbiting = true;
	}
	else
	{
		bOrbiting = false;
	}
	LastMouse = FVector2D(MX, MY);
	OrbitYaw += 90.0f * DeltaSeconds * ((PC->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(EKeys::Left) ? 1.0f : 0.0f));
	OrbitPitch = FMath::Clamp(OrbitPitch + 60.0f * DeltaSeconds * ((PC->IsInputKeyDown(EKeys::Up) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f)), -85.0f, 85.0f);
	if (Pressed(EKeys::MouseScrollUp)) OrbitDistance = FMath::Max(OrbitDistance * 0.9f, 300.0f);
	if (Pressed(EKeys::MouseScrollDown)) OrbitDistance = FMath::Min(OrbitDistance / 0.9f, 3000.0f);

	if (Pressed(EKeys::PageUp)) StartLevel(LevelIndex + 1);
	if (Pressed(EKeys::PageDown)) StartLevel(LevelIndex - 1);

	if (Phase == ERainShieldPhase::Plan)
	{
		// Moves follow the view: forward/right snap to the grid axis closest to where the camera looks.
		const FVector Look = FRotator(0.0f, OrbitYaw, 0.0f).Vector();
		const FIntVector Fwd = FMath::Abs(Look.X) >= FMath::Abs(Look.Y) ? FIntVector(Look.X > 0 ? 1 : -1, 0, 0) : FIntVector(0, Look.Y > 0 ? 1 : -1, 0);
		const FIntVector Right(-Fwd.Y, Fwd.X, 0);
		if (Pressed(EKeys::W)) MovePiece(Fwd);
		if (Pressed(EKeys::S)) MovePiece(FIntVector::ZeroValue - Fwd);
		if (Pressed(EKeys::D)) MovePiece(Right);
		if (Pressed(EKeys::A)) MovePiece(FIntVector::ZeroValue - Right);
		if (Pressed(EKeys::E)) MovePiece(FIntVector(0, 0, 1));
		if (Pressed(EKeys::Q)) MovePiece(FIntVector(0, 0, -1));
		if (Pressed(EKeys::R)) RotatePiece(2);
		if (Pressed(EKeys::T)) RotatePiece(Right.X != 0 ? 0 : 1);
		if (Pressed(EKeys::Tab)) SelectNextPiece();
		if (Pressed(EKeys::SpaceBar)) PlacePiece();
		if (Pressed(EKeys::BackSpace)) UndoPiece();
		const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
		for (int32 i = 0; i < 6; ++i)
		{
			if (Pressed(Numbers[i])) SetDirection(i);
		}
		if (Pressed(EKeys::X)) SetDirection(DirectionIndex + 1);
		if (Pressed(EKeys::Z)) SetDirection(DirectionIndex - 1);
		if (Pressed(EKeys::G)) SetHailMode(!bHailMode);
		if (Pressed(EKeys::Enter)) StartRound();
	}
	else if (Phase == ERainShieldPhase::Result)
	{
		if (Pressed(EKeys::Enter))
		{
			StartLevel(GetStars() > 0 ? LevelIndex + 1 : LevelIndex);
		}
		if (Pressed(EKeys::R))
		{
			// Back to planning with the same pieces in place.
			Phase = ERainShieldPhase::Plan;
			RoundClock = 0.0f;
			Hits = 0;
			HitWaterMl = 0.0;
			UploadBoxes(false);
			UploadRain();
		}
	}
}

void ARainShieldGame::SetOrbit(float Yaw, float Pitch, float Distance)
{
	OrbitYaw = Yaw;
	OrbitPitch = FMath::Clamp(Pitch, -85.0f, 85.0f);
	OrbitDistance = FMath::Clamp(Distance, 300.0f, 3000.0f);
	UpdateCamera();
}

void ARainShieldGame::UpdateCamera()
{
	if (!Camera)
	{
		return;
	}
	const FRotator Rot(OrbitPitch, OrbitYaw, 0.0f);
	Camera->SetActorLocationAndRotation(GridCenter() - Rot.Vector() * OrbitDistance, Rot);
	if (UCineCameraComponent* Cine = Camera->GetCineCameraComponent())
	{
		Cine->FocusSettings.ManualFocusDistance = OrbitDistance;
	}
	if (Backlight)
	{
		const FVector Fwd = Rot.Vector();
		Backlight->SetActorLocation(GridCenter() + Fwd * BacklightDistance + FVector::UpVector * BacklightRise);
	}
}

void ARainShieldGame::DrawPlan() const
{
	UWorld* World = GetWorld();
	const float G = GridSize * CellSize;
	DrawDebugBox(World, GridCenter(), FVector(0.5f * G), FColor(90, 110, 140), false, -1.0f, SDPG_World, 1.0f);
	if (Phase != ERainShieldPhase::Plan)
	{
		return;
	}
	// The rain's direction: an arrow from the stream's face to the grid.
	const FVector D = Direction();
	DrawDebugDirectionalArrow(World, GridCenter() - D * (0.5f * G + 260.0f), GridCenter() - D * (0.5f * G + 30.0f), 60.0f, FColor(80, 200, 255), false, -1.0f, SDPG_Foreground, 5.0f);

	// The piece being placed, and the grid layer it sits on.
	const TArray<FIntVector> Ghost = GhostCells();
	if (Ghost.Num() > 0)
	{
		const bool bFree = IsFree(Ghost);
		for (const FIntVector& C : Ghost)
		{
			DrawDebugSolidBox(World, CellCenter(C), FVector(0.5f * CellSize * 0.96f), bFree ? FColor(60, 220, 120, 90) : FColor(240, 60, 60, 90), false, -1.0f, SDPG_World);
			DrawDebugBox(World, CellCenter(C), FVector(0.5f * CellSize * 0.96f), bFree ? FColor(60, 220, 120) : FColor(240, 60, 60), false, -1.0f, SDPG_Foreground, 1.5f);
		}
		const float Z = GridCenter().Z + (PieceAnchor.Z - 0.5f * GridSize) * CellSize;
		const FVector Corner = GridCenter() - FVector(0.5f * G, 0.5f * G, 0.0f);
		for (int32 i = 0; i <= GridSize; ++i)
		{
			DrawDebugLine(World, FVector(Corner.X + i * CellSize, Corner.Y, Z), FVector(Corner.X + i * CellSize, Corner.Y + G, Z), FColor(70, 90, 120), false, -1.0f, SDPG_World, 0.5f);
			DrawDebugLine(World, FVector(Corner.X, Corner.Y + i * CellSize, Z), FVector(Corner.X + G, Corner.Y + i * CellSize, Z), FColor(70, 90, 120), false, -1.0f, SDPG_World, 0.5f);
		}
	}
}

void ARainShieldGame::ShowHud() const
{
	if (!GEngine)
	{
		return;
	}
	// On-screen messages stack upwards: the last one added is the top line.
	const FRainShieldLevel& Level = Levels[LevelIndex];
	const uint64 Key = 0x5348494530ull;
	const FVector2D Big(1.4f, 1.4f);
	TArray<TPair<FColor, FString>> Lines;
	auto Line = [&Lines](const FColor& Color, const FString& Text) { Lines.Add({ Color, Text }); };
	static const TCHAR* PhaseNames[] = { TEXT("PLAN"), TEXT("FALLING"), TEXT("RESULT") };
	const TCHAR* Weather = bHailMode ? TEXT("HAIL") : TEXT("RAIN");
	Line(FColor(230, 235, 255), FString::Printf(TEXT("RAIN SHIELD   level %d/%d: %s   [%s]   weather: %s"), LevelIndex + 1, Levels.Num(), *Level.Name, PhaseNames[int32(Phase)], Weather));
	auto StarText = [](int32 Stars) { return FString::ChrN(Stars, TEXT('*')) + FString::ChrN(3 - Stars, TEXT('-')); };
	if (Phase == ERainShieldPhase::Plan)
	{
		FString Pieces;
		for (int32 i = 0; i < Level.Pieces.Num(); ++i)
		{
			const bool bPlaced = Placed.ContainsByPredicate([i](const FPlaced& P) { return P.Piece == i; });
			const FString& Name = Level.Pieces[i].Name;
			Pieces += i == CurrentPiece && PieceCells.Num() > 0 ? TEXT("[") + Name + TEXT("]  ") : (bPlaced ? TEXT("(") + Name + TEXT(" placed)  ") : Name + TEXT("  "));
		}
		Line(FColor(200, 220, 255), FString::Printf(TEXT("Pieces: %s"), *Pieces));
		Line(FColor(80, 200, 255), FString::Printf(TEXT("%s falls: %s   (locked when it starts)"), Weather, *DirectionName(DirectionIndex)));
		Line(FColor(150, 160, 180), TEXT("WASD/QE move   R/T rotate   Tab next piece   Space place   Backspace undo   1-6, Z/X direction   G rain/hail   Enter: start   RMB/arrows orbit   wheel zoom   PgUp/PgDn level"));
	}
	else
	{
		const FColor C = Phase == ERainShieldPhase::Result ? FColor(255, 220, 120) : FColor(200, 220, 255);
		Line(C, FString::Printf(TEXT("Time %.1f / %.0f s   hits %d (%.1f ml)   wetness %.1f%%   %s"),
			RoundClock, Level.Duration, Hits, HitWaterMl, 100.0f * GetWetness(), *StarText(GetStars())));
		FString Extra;
		if (bHailMode)
		{
			Extra = FString::Printf(TEXT("   blocker impacts %d, broken %d"), BlockerImpacts, BrokenBlockers);
			for (const FPlaced& P : Placed)
			{
				Extra += FString::Printf(TEXT("   %s %.0f%%"), *Level.Pieces[P.Piece].Name, 100.0f * P.HitPoints / FMath::Max(P.MaxHitPoints, 1));
			}
		}
		Line(FColor(80, 200, 255), FString::Printf(TEXT("%s falls: %s   stars at wetness <= %.0f%% / %.0f%% / %.0f%%%s"),
			Weather, *DirectionName(DirectionIndex), 100.0f * StarWetness.X, 100.0f * StarWetness.Y, 100.0f * StarWetness.Z, *Extra));
		Line(FColor(150, 160, 180), Phase == ERainShieldPhase::Result
			? (GetStars() > 0 ? TEXT("Enter: next level   R: re-plan") : TEXT("Too wet. Enter: try again   R: re-plan")) : TEXT("RMB/arrows orbit   wheel zoom"));
	}
	if (FlashTime > 0.0f)
	{
		Line(FColor(255, 120, 90), Flash);
	}
	Line(FColor(120, 200, 140), bHailMode
		? FString::Printf(TEXT("System 2 %s meshes: %d requested (rain.game.HailCount), %d live   boxes %d/%d   impacts read back: max %d/frame"),
			bHailGpu ? TEXT("GPU") : TEXT("CPU"), HailCount, LiveCount(Hail), BoxesUsed, RainShield::MaxBoxes, MaxHitsPerCallback)
		: FString::Printf(TEXT("System 1 GPU sprites: %d requested (rain.game.Count), %d live   boxes %d/%d   hit readback: max %d/frame of %d%s"),
			RainCount, LiveCount(Rain), BoxesUsed, RainShield::MaxBoxes, MaxHitsPerCallback, RainShield::ExportCapacity,
			SaturatedCallbacks > 0 ? *FString::Printf(TEXT("   SATURATED %d frames"), SaturatedCallbacks) : TEXT("")));
	for (int32 i = Lines.Num() - 1, Slot = 0; i >= 0; --i, ++Slot)
	{
		GEngine->AddOnScreenDebugMessage(Key + Slot, 0.0f, Lines[i].Key, Lines[i].Value, false, Slot == Lines.Num() - 1 ? Big : FVector2D(1.15f, 1.15f));
	}
	for (int32 Slot = Lines.Num(); Slot < 8; ++Slot)
	{
		GEngine->RemoveOnScreenDebugMessage(Key + Slot);
	}
}

// ---------------------------------------------------------------------------------------------- sweep

void ARainShieldGame::StartSweep(const TArray<int32>& Counts, bool bHail, float SettleSeconds, float MeasureSeconds)
{
	if (Counts.Num() == 0)
	{
		return;
	}
	if (GEngine)
	{
		// Uncapped: the frame time is the work, not a frame limiter or vsync. stat unit: the thread and GPU times
		// read below (also on screen, for the video).
		GEngine->Exec(GetWorld(), TEXT("t.MaxFPS 0"));
		GEngine->Exec(GetWorld(), TEXT("r.VSync 0"));
		GEngine->Exec(GetWorld(), TEXT("fx.ParticlePerfStats.Enabled 1"));
		// Render at the window's full resolution: by default the desktop screen percentage follows the display
		// (72.9% of 1920x1080 on a Retina Mac).
		GEngine->Exec(GetWorld(), TEXT("r.ScreenPercentage.Default.Desktop.Mode 0"));
		GEngine->Exec(GetWorld(), TEXT("r.ScreenPercentage 100"));
		GEngine->Exec(GetWorld(), TEXT("r.DynamicRes.OperationMode 0"));
		if (GEngine->GameViewport && !GEngine->GameViewport->IsStatEnabled(TEXT("Unit")))
		{
			GEngine->Exec(GetWorld(), TEXT("stat unit"));
		}
	}
	// A scored round that never ends, in the selected weather.
	bSweepHail = bHail;
	SweepIndex = 0;
	bHailMode = bHail;
	if (Phase == ERainShieldPhase::Plan)
	{
		StartRound();
	}
	Phase = ERainShieldPhase::Raining;
	UploadRain();
	SweepCounts = Counts;
	SweepSettle = FMath::Max(SettleSeconds, 0.5f);
	SweepMeasure = FMath::Max(MeasureSeconds, 0.5f);
	SweepCsv = TEXT("system,count_requested,count_live,frames,frame_ms,frame_ms_max,fps,game_thread_ms,render_thread_ms,gpu_ms,niagara_gt_ms,niagara_rt_ms,niagara_gpu_ms,events_per_s,resolution,screen_percentage\n");
	bSweepMeasuring = false;
	SweepTimer = 0.0f;
	bHail ? SetHailCount(SweepCounts[0]) : SetRainCount(SweepCounts[0]);
	UE_LOG(LogTemp, Display, TEXT("rain.game.Sweep: %s, %d levels"), bHail ? TEXT("hail") : TEXT("rain"), SweepCounts.Num());
}

void ARainShieldGame::TickSweep(float DeltaSeconds)
{
	if (SweepIndex == INDEX_NONE)
	{
		return;
	}
	const float RealDt = float(FApp::GetDeltaTime());
	SweepTimer += RealDt;
	const int32 Requested = bSweepHail ? HailCount : RainCount;
	if (!bSweepMeasuring)
	{
		// Settle: the new instance has spawned (GPU counts are read back a few frames late) and filled the stream.
		const int32 Live = LiveCount(bSweepHail ? Hail : Rain);
		const bool bCounted = FMath::Abs(Live - Requested) <= Requested / 100;
		if (SweepTimer >= SweepSettle && (bCounted || SweepTimer > SweepSettle + 5.0f))
		{
			bSweepMeasuring = true;
			SweepTimer = 0.0f;
			SweepFrames = 0;
			SweepFrameMs = SweepFrameMsMax = SweepGameMs = SweepRenderMs = SweepGpuMs = 0.0;
			SweepHitsAtStart = Hits + BlockerImpacts;
			SweepListener = MakeShared<FParticlePerfStatsListener_GatherAll, ESPMode::ThreadSafe>(false, true, false);
			FParticlePerfStatsManager::AddListener(SweepListener);
			// -RainLateExec="cmd,cmd": console commands once the first level is measuring (e.g. ProfileGPU).
			FString LateExec;
			if (SweepIndex == 0 && GEngine && FParse::Value(FCommandLine::Get(), TEXT("RainLateExec="), LateExec, false))
			{
				TArray<FString> Commands;
				LateExec.ParseIntoArray(Commands, TEXT(","));
				for (const FString& Command : Commands)
				{
					GEngine->Exec(GetWorld(), *Command.TrimStartAndEnd());
				}
			}
		}
		return;
	}
	++SweepFrames;
	SweepFrameMs += RealDt * 1000.0;
	SweepFrameMsMax = FMath::Max(SweepFrameMsMax, RealDt * 1000.0);
	// stat unit's raw per-frame times (ms).
	if (const FStatUnitData* Unit = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetStatUnitData() : nullptr)
	{
		SweepGameMs += Unit->RawGameThreadTime;
		SweepRenderMs += Unit->RawRenderThreadTime;
		SweepGpuMs += Unit->RawGPUFrameTime[0];
	}
	if (SweepTimer >= SweepMeasure)
	{
		FinishSweepLevel();
	}
}

void ARainShieldGame::FinishSweepLevel()
{
	UNiagaraComponent* Target = bSweepHail ? Hail : Rain;
	const double N = FMath::Max(SweepFrames, 1);
	double NiagaraGt = 0.0, NiagaraRt = 0.0, NiagaraGpu = 0.0;
#if WITH_PER_SYSTEM_PARTICLE_PERF_STATS
	if (SweepListener)
	{
		if (FAccumulatedParticlePerfStats* Stats = SweepListener->GetStats(Target->GetAsset()))
		{
			// Per frame, in microseconds.
			NiagaraGt = Stats->GetGameThreadStats().GetPerFrameAvg() / 1000.0;
			NiagaraRt = Stats->GetRenderThreadStats(true).GetPerFrameAvg() / 1000.0;
			NiagaraGpu = Stats->GetGPUStats(true).GetPerFrameAvgMicroseconds() / 1000.0;
		}
	}
#endif
	if (SweepListener)
	{
		FParticlePerfStatsManager::RemoveListener(SweepListener.Get());
		SweepListener.Reset();
	}
	FIntPoint Resolution(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Resolution = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	const TCHAR* SystemName = bSweepHail ? (bHailGpu ? TEXT("System2_hail_GPU_meshes") : TEXT("System2_hail_CPU_meshes")) : TEXT("System1_rain_GPU_sprites");
	const double FrameMs = SweepFrameMs / N;
	static IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	const FString Row = FString::Printf(TEXT("%s,%d,%d,%d,%.3f,%.3f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%dx%d,%.0f\n"),
		SystemName, bSweepHail ? HailCount : RainCount, LiveCount(Target), SweepFrames, FrameMs, SweepFrameMsMax, 1000.0 / FMath::Max(FrameMs, 1e-3),
		SweepGameMs / N, SweepRenderMs / N, SweepGpuMs / N, NiagaraGt, NiagaraRt, NiagaraGpu,
		(Hits + BlockerImpacts - SweepHitsAtStart) / FMath::Max(SweepTimer, 1e-3f), Resolution.X, Resolution.Y,
		ScreenPercentage ? ScreenPercentage->GetFloat() : 0.0f);
	SweepCsv += Row;
	UE_LOG(LogTemp, Display, TEXT("rain.game.Sweep %s"), *Row.TrimEnd());

	++SweepIndex;
	// Past ~4 fps the next level only risks a GPU timeout.
	if (SweepIndex >= SweepCounts.Num() || FrameMs > 250.0)
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Sweeps");
		IFileManager::Get().MakeDirectory(*Dir, true);
		LastSweepFile = FPaths::ConvertRelativePathToFull(Dir / FString::Printf(TEXT("RainShield_%s_%s.csv"), SystemName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		FFileHelper::SaveStringToFile(SweepCsv, *LastSweepFile);
		UE_LOG(LogTemp, Display, TEXT("rain.game.Sweep: wrote %s"), *LastSweepFile);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(0x5348494540ull, 10.0f, FColor::Green, FString::Printf(TEXT("Sweep written: %s"), *LastSweepFile));
		}
		SweepIndex = INDEX_NONE;
		bSweepHail ? SetHailCount(FMath::Max(CVarRainGameHailCount.GetValueOnGameThread(), 0)) : SetRainCount(FMath::Max(CVarRainGameCount.GetValueOnGameThread(), 0));
		if (bQuitAfterSweep && GEngine)
		{
			GEngine->Exec(GetWorld(), TEXT("quit"));
		}
		return;
	}
	bSweepMeasuring = false;
	SweepTimer = 0.0f;
	bSweepHail ? SetHailCount(SweepCounts[SweepIndex]) : SetRainCount(SweepCounts[SweepIndex]);
}

FString ARainShieldGame::DebugHailParticles(int32 Max) const
{
	FString Out;
	FNiagaraSystemInstanceControllerConstPtr SIC = Hail ? Hail->GetSystemInstanceController() : nullptr;
	const FNiagaraSystemInstance* Instance = SIC ? SIC->GetSystemInstance_Unsafe() : nullptr;
	if (!Instance)
	{
		return TEXT("no hail instance");
	}
	for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
	{
		if (Emitter->GetGPUContext())
		{
			Out += TEXT("GPU emitter: not readable here\n");
			continue;
		}
		const FNiagaraDataSet& Data = Emitter->GetParticleData();
		const FNiagaraDataBuffer* Buffer = Data.GetCurrentData();
		const int32 Num = Buffer ? int32(Buffer->GetNumInstances()) : 0;
		auto Pos = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position"));
		auto Vel = FNiagaraDataSetAccessor<FVector3f>::CreateReader(Data, TEXT("Velocity"));
		auto State = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("HailState"));
		auto Radius = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("HailRadius"));
		auto Impact = FNiagaraDataSetAccessor<FVector3f>::CreateReader(Data, TEXT("HailImpact"));
		auto Timer = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("HailTimer"));
		auto Cycle = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("HailCycle"));
		auto Angle = FNiagaraDataSetAccessor<float>::CreateReader(Data, TEXT("HailAngle"));
		Out += FString::Printf(TEXT("%d particles\n"), Num);
		for (int32 i = 0; i < Num && i < Max; ++i)
		{
			const int32 j = i * FMath::Max(Num / FMath::Max(Max, 1), 1);
			Out += FString::Printf(TEXT("%d pos %s vel %s state %.2f r %.2f impact %s timer %g cycle %g angle %g\n"), j, *FVector3f(Pos.Get(j)).ToString(), *Vel.Get(j).ToString(),
				State.Get(j), Radius.Get(j), *Impact.Get(j).ToString(), Timer.Get(j), Cycle.Get(j), Angle.Get(j));
		}
	}
	return Out;
}
