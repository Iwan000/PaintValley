#include "RainShieldGame.h"

#include "RainMagicController.h"
#include "RainShieldTypes.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "DrawDebugHelpers.h"
#include "DynamicRHI.h"
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
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Particles/ParticlePerfStatsManager.h"
#include "RenderTimer.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------------------------------------- console
// Graders reproduce the performance numbers with these (see README).

static TAutoConsoleVariable<int32> CVarRainGameCount(
	TEXT("rain.game.Count"),
	RainShield::DefaultRainCount,
	TEXT("Rain Shield: drops in the game's rain (System 1, GPU sprites). Changing it reinitialises the rain system."),
	ECVF_Default);

static FAutoConsoleCommandWithWorldAndArgs GRainGameSweep(
	TEXT("rain.game.Sweep"),
	TEXT("Rain Shield: steps the rain through particle counts and writes frame / game thread / render thread / GPU / Niagara times to Saved/Sweeps/*.csv.\n")
	TEXT("Args: counts (default 0 50000 100000 200000 400000 800000 1600000), optional settle=<s> measure=<s>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		TArray<int32> Counts;
		float Settle = 3.0f;
		float Measure = 5.0f;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("settle=")))
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
			Counts = { 0, 50000, 100000, 200000, 400000, 800000, 1600000 };
		}
		for (TActorIterator<ARainShieldGame> It(World); It; ++It)
		{
			It->StartSweep(Counts, Settle, Measure);
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
	if (UNiagaraSystem* System = RainSystem.LoadSynchronous())
	{
		Rain->SetAsset(System);
		Rain->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
		Rain->SetVariableObject(TEXT("RainHitHandler"), this);
		Rain->SetVariableInt(TEXT("RainGameCount"), RainCount);
		// A 180-degree shutter at 60 fps: short streaks.
		Rain->SetVariableFloat(TEXT("RainShutter"), 0.5f / 60.0f);
		Rain->SetVariableFloat(TEXT("RainGameGust"), Gust);
		Rain->Activate(true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("RainShieldGame: %s missing (run unreal.RainBuilderLibrary.build_rain_game_assets())"), *RainSystem.ToString());
	}
	StartLevel(0);
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
		const FVector Lo = bUsed ? Boxes[i].Key.Min : FVector(1e9);
		const FVector Hi = bUsed ? Boxes[i].Key.Max : FVector(-1e9);
		Rain->SetVariableVec4(*FString::Printf(TEXT("RainBox%dMin"), i), FVector4(Lo.X, Lo.Y, Lo.Z, bUsed ? Boxes[i].Value : RainShield::BoxNone));
		Rain->SetVariableVec3(*FString::Printf(TEXT("RainBox%dMax"), i), Hi);
	}
}

void ARainShieldGame::UploadRain()
{
	Rain->SetVariableVec3(TEXT("RainGameDir"), Direction());
	Rain->SetVariableVec3(TEXT("RainGameCenter"), GridCenter());
	Rain->SetVariableFloat(TEXT("RainGameHalf"), StreamHalf());
	Rain->SetVariableFloat(TEXT("RainGameLength"), StreamLength());
	Rain->SetVariableFloat(TEXT("RainGameActive"), Phase == ERainShieldPhase::Plan ? 0.0f : 1.0f);
	Rain->SetVariableFloat(TEXT("RainGameScoring"), Phase == ERainShieldPhase::Raining ? 1.0f : 0.0f);
	Rain->SetVariableFloat(TEXT("RainGameGust"), Gust);
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
	Placed.Add({ CurrentPiece, Cells });
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
	const float Density = RainCount / (4.0f * H * H * L);
	const float Arrive = (0.5f * L - 0.5f * GridSize * CellSize) / FMath::Max(MeanFallSpeed, 1.0f);
	return Density * MeanFallSpeed * ReferenceArea * FMath::Max(Seconds - Arrive, 0.0f);
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

// ---------------------------------------------------------------------------------------------- tick

void ARainShieldGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!bViewSet && PC && Camera)
	{
		PC->SetViewTarget(Camera);
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeGameAndUI());
		bViewSet = true;
	}

	const int32 WantedCount = FMath::Max(CVarRainGameCount.GetValueOnGameThread(), 0);
	if (SweepIndex == INDEX_NONE && WantedCount != RainCount)
	{
		SetRainCount(WantedCount);
	}

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
	const FRainShieldLevel& Level = Levels[LevelIndex];
	const uint64 Key = 0x5348494530ull;
	const FVector2D Big(1.4f, 1.4f);
	static const TCHAR* PhaseNames[] = { TEXT("PLAN"), TEXT("RAIN"), TEXT("RESULT") };
	GEngine->AddOnScreenDebugMessage(Key + 0, 0.0f, FColor(230, 235, 255),
		FString::Printf(TEXT("RAIN SHIELD   level %d/%d: %s   [%s]"), LevelIndex + 1, Levels.Num(), *Level.Name, PhaseNames[int32(Phase)]), false, Big);

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
		GEngine->AddOnScreenDebugMessage(Key + 1, 0.0f, FColor(200, 220, 255), FString::Printf(TEXT("Pieces: %s"), *Pieces), false, Big);
		GEngine->AddOnScreenDebugMessage(Key + 2, 0.0f, FColor(80, 200, 255), FString::Printf(TEXT("Rain falls: %s   (locked when the rain starts)"), *DirectionName(DirectionIndex)), false, Big);
		GEngine->AddOnScreenDebugMessage(Key + 3, 0.0f, FColor(150, 160, 180),
			TEXT("WASD/QE move   R/T rotate   Tab next piece   Space place   Backspace undo   1-6, Z/X rain direction   Enter: start the rain   RMB/arrows orbit   wheel zoom   PgUp/PgDn level"));
	}
	else
	{
		const FColor C = Phase == ERainShieldPhase::Result ? FColor(255, 220, 120) : FColor(200, 220, 255);
		GEngine->AddOnScreenDebugMessage(Key + 1, 0.0f, C, FString::Printf(TEXT("Time %.1f / %.0f s   hits %d (%.1f ml)   wetness %.1f%%   %s"),
			RoundClock, Level.Duration, Hits, HitWaterMl, 100.0f * GetWetness(), *StarText(GetStars())), false, Big);
		GEngine->AddOnScreenDebugMessage(Key + 2, 0.0f, FColor(80, 200, 255), FString::Printf(TEXT("Rain falls: %s   stars at wetness <= %.0f%% / %.0f%% / %.0f%%"),
			*DirectionName(DirectionIndex), 100.0f * StarWetness.X, 100.0f * StarWetness.Y, 100.0f * StarWetness.Z), false, Big);
		GEngine->AddOnScreenDebugMessage(Key + 3, 0.0f, FColor(150, 160, 180), Phase == ERainShieldPhase::Result
			? (GetStars() > 0 ? TEXT("Enter: next level   R: re-plan") : TEXT("Too wet. Enter: try again   R: re-plan")) : TEXT("RMB/arrows orbit   wheel zoom"));
	}
	GEngine->AddOnScreenDebugMessage(Key + 4, 0.0f, FColor(120, 200, 140), FString::Printf(
		TEXT("System 1 GPU sprites: %d requested (rain.game.Count), %d live   boxes %d/%d   hit readback: max %d/frame of %d%s"),
		RainCount, GetLiveParticleCount(), BoxesUsed, RainShield::MaxBoxes, MaxHitsPerCallback, RainShield::ExportCapacity,
		SaturatedCallbacks > 0 ? *FString::Printf(TEXT("   SATURATED %d frames"), SaturatedCallbacks) : TEXT("")));
}

// ---------------------------------------------------------------------------------------------- sweep

void ARainShieldGame::StartSweep(const TArray<int32>& Counts, float SettleSeconds, float MeasureSeconds)
{
	if (Counts.Num() == 0)
	{
		return;
	}
	if (GEngine)
	{
		// Uncapped: the frame time is the work, not a frame limiter or vsync.
		GEngine->Exec(GetWorld(), TEXT("t.MaxFPS 0"));
		GEngine->Exec(GetWorld(), TEXT("r.VSync 0"));
		GEngine->Exec(GetWorld(), TEXT("fx.ParticlePerfStats.Enabled 1"));
	}
	if (Phase == ERainShieldPhase::Plan)
	{
		StartRound();
	}
	SweepCounts = Counts;
	SweepSettle = FMath::Max(SettleSeconds, 0.5f);
	SweepMeasure = FMath::Max(MeasureSeconds, 0.5f);
	SweepCsv = TEXT("system,count_requested,count_live,frames,frame_ms,frame_ms_max,fps,game_thread_ms,render_thread_ms,gpu_ms,niagara_gt_ms,niagara_rt_ms,niagara_gpu_ms,hits_per_s\n");
	SweepIndex = 0;
	bSweepMeasuring = false;
	SweepTimer = 0.0f;
	SetRainCount(SweepCounts[0]);
	UE_LOG(LogTemp, Display, TEXT("rain.game.Sweep: %d levels"), SweepCounts.Num());
}

void ARainShieldGame::TickSweep(float DeltaSeconds)
{
	if (SweepIndex == INDEX_NONE)
	{
		return;
	}
	const float RealDt = float(FApp::GetDeltaTime());
	SweepTimer += RealDt;
	if (!bSweepMeasuring)
	{
		// Settle: the new instance has spawned (the GPU count is read back a few frames late) and filled the stream.
		const int32 Live = GetLiveParticleCount();
		const bool bCounted = FMath::Abs(Live - RainCount) <= FMath::Max(RainCount / 100, 0);
		if (SweepTimer >= SweepSettle && (bCounted || SweepTimer > SweepSettle + 5.0f))
		{
			bSweepMeasuring = true;
			SweepTimer = 0.0f;
			SweepFrames = 0;
			SweepFrameMs = SweepFrameMsMax = SweepGameMs = SweepRenderMs = SweepGpuMs = 0.0;
			SweepHitsAtStart = Hits;
			SweepListener = MakeShared<FParticlePerfStatsListener_GatherAll, ESPMode::ThreadSafe>(false, true, false);
			FParticlePerfStatsManager::AddListener(SweepListener);
		}
		return;
	}
	++SweepFrames;
	SweepFrameMs += RealDt * 1000.0;
	SweepFrameMsMax = FMath::Max(SweepFrameMsMax, RealDt * 1000.0);
	SweepGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
	SweepRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	SweepGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	if (SweepTimer >= SweepMeasure)
	{
		FinishSweepLevel();
	}
}

void ARainShieldGame::FinishSweepLevel()
{
	const double N = FMath::Max(SweepFrames, 1);
	double NiagaraGt = 0.0, NiagaraRt = 0.0, NiagaraGpu = 0.0;
#if WITH_PER_SYSTEM_PARTICLE_PERF_STATS
	if (SweepListener)
	{
		if (FAccumulatedParticlePerfStats* Stats = SweepListener->GetStats(Rain->GetAsset()))
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
	const double FrameMs = SweepFrameMs / N;
	const FString Row = FString::Printf(TEXT("System1_GPU_sprites,%d,%d,%d,%.3f,%.3f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f\n"),
		RainCount, GetLiveParticleCount(), SweepFrames, FrameMs, SweepFrameMsMax, 1000.0 / FMath::Max(FrameMs, 1e-3),
		SweepGameMs / N, SweepRenderMs / N, SweepGpuMs / N, NiagaraGt, NiagaraRt, NiagaraGpu, (Hits - SweepHitsAtStart) / FMath::Max(SweepTimer, 1e-3f));
	SweepCsv += Row;
	UE_LOG(LogTemp, Display, TEXT("rain.game.Sweep %s"), *Row.TrimEnd());

	++SweepIndex;
	// Past ~4 fps the next level only risks a GPU timeout.
	if (SweepIndex >= SweepCounts.Num() || FrameMs > 250.0)
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Sweeps");
		IFileManager::Get().MakeDirectory(*Dir, true);
		LastSweepFile = FPaths::ConvertRelativePathToFull(Dir / FString::Printf(TEXT("RainShield_System1_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		FFileHelper::SaveStringToFile(SweepCsv, *LastSweepFile);
		UE_LOG(LogTemp, Display, TEXT("rain.game.Sweep: wrote %s"), *LastSweepFile);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(0x5348494540ull, 10.0f, FColor::Green, FString::Printf(TEXT("Sweep written: %s"), *LastSweepFile));
		}
		SweepIndex = INDEX_NONE;
		SetRainCount(FMath::Max(CVarRainGameCount.GetValueOnGameThread(), 0));
		return;
	}
	bSweepMeasuring = false;
	SweepTimer = 0.0f;
	SetRainCount(SweepCounts[SweepIndex]);
}
