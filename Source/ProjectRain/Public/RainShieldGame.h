// Project Rain - Rain Shield: the game. A shape floats in a 3D grid; the player places the blocker pieces the
// level gives them, picks the direction the rain falls from, and then the rain (NS_RainGame, GPU sprites) falls
// for a fixed time. Blockers absorb drops; every drop that reaches the shape is read back from the GPU
// (Export Particle Data -> ReceiveParticleData) and the shape's wetness decides the level's stars.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraDataInterfaceExport.h"
#include "RainShieldGame.generated.h"

class ARainMagicController;
class ACineCameraActor;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;
class FParticlePerfStatsListener_GatherAll;

UENUM(BlueprintType)
enum class ERainShieldPhase : uint8
{
	/** Look at the shape, place the pieces, pick the rain's direction. */
	Plan,
	/** The rain falls; hits are scored. */
	Raining,
	/** Time is up: wetness and stars. */
	Result,
};

/** A blocker piece: unit cells relative to its anchor. */
USTRUCT(BlueprintType)
struct FRainShieldPiece
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain Shield") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain Shield") TArray<FIntVector> Cells;
};

USTRUCT(BlueprintType)
struct FRainShieldLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain Shield") FString Name;
	/** Grid cells of the shape to keep dry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain Shield") TArray<FIntVector> Protected;
	/** The pieces the player is given. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain Shield") TArray<FRainShieldPiece> Pieces;
	/** Seconds of rain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain Shield") float Duration = 20.0f;
};

UCLASS()
class PROJECTRAIN_API ARainShieldGame : public AActor, public INiagaraParticleCallbackHandler
{
	GENERATED_BODY()

public:
	ARainShieldGame();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Export Particle Data calls this on the game thread with the drops that hit the shape (a few frames late). */
	virtual void ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data, UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset) override;

	// ------------------------------------------------------------------ components and references
	UPROPERTY(VisibleAnywhere, Category = "Rain Shield") TObjectPtr<UNiagaraComponent> Rain;
	UPROPERTY(VisibleAnywhere, Category = "Rain Shield") TObjectPtr<UInstancedStaticMeshComponent> ProtectedCubes;
	UPROPERTY(VisibleAnywhere, Category = "Rain Shield") TObjectPtr<UInstancedStaticMeshComponent> BlockerCubes;

	UPROPERTY(EditAnywhere, Category = "Rain Shield") TSoftObjectPtr<UNiagaraSystem> RainSystem;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") TObjectPtr<UMaterialInterface> CubeMaterial;
	/** The film rain's controller: lens, lamps and camera inputs for the drops' look (found in the level if unset). */
	UPROPERTY(EditAnywhere, Category = "Rain Shield") TObjectPtr<ARainMagicController> Controller;

	// ------------------------------------------------------------------ grid and rain
	UPROPERTY(EditAnywhere, Category = "Rain Shield") int32 GridSize = 10;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float CellSize = 30.0f;
	/** Height of the grid's centre above the actor (cm). */
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float GridHeight = 250.0f;
	/** Gusts across the rain (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float Gust = 60.0f;
	/** Wetness (hits / hits expected on the unshielded shape) for 3, 2 and 1 stars. */
	UPROPERTY(EditAnywhere, Category = "Rain Shield") FVector StarWetness = FVector(0.04, 0.12, 0.30);
	UPROPERTY(EditAnywhere, Category = "Rain Shield") TArray<FRainShieldLevel> Levels;

	// ------------------------------------------------------------------ camera
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float OrbitYaw = -35.0f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float OrbitPitch = -20.0f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float OrbitDistance = 950.0f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float FocalLength = 24.0f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float FStop = 8.0f;
	/** The stage is exposed (manual, physical camera exposure) for the film cameras' f/2.2: compensate for FStop. */
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float ReferenceFStop = 2.2f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float ExposureBias = 2.5f;
	/** Rain shows only against a light behind it (forward scattering): a lamp kept behind the grid, opposite the
	 *  orbiting camera and a little above its line of sight. It lights only the drops' glints (lighting channel 1). */
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float BacklightIntensity = 15000.0f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float BacklightDistance = 1200.0f;
	UPROPERTY(EditAnywhere, Category = "Rain Shield") float BacklightRise = 300.0f;

	// ------------------------------------------------------------------ API (keys in PIE; Blueprint/Python too)
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void StartLevel(int32 Index);
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void SelectNextPiece();
	/** Moves the piece being placed by whole cells (grid axes). */
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void MovePiece(FIntVector Delta);
	/** Turns the piece being placed a quarter turn about a grid axis (0 = X, 1 = Y, 2 = Z). */
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void RotatePiece(int32 Axis);
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") bool PlacePiece();
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void UndoPiece();
	/** One of the 26 directions (0-5: the axes, then the edge and corner diagonals). */
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void SetDirection(int32 Index);
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void StartRound();
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void SetOrbit(float Yaw, float Pitch, float Distance);
	/** Drops in the rain (rain.game.Count does the same): reinitialises the rain system. */
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void SetRainCount(int32 Count);
	/** Measures frame, thread, GPU and Niagara times at each particle count (rain.game.Sweep). Writes a CSV to Saved/Sweeps. */
	UFUNCTION(BlueprintCallable, Category = "Rain Shield") void StartSweep(const TArray<int32>& Counts, float SettleSeconds = 3.0f, float MeasureSeconds = 5.0f);

	UFUNCTION(BlueprintPure, Category = "Rain Shield") ERainShieldPhase GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category = "Rain Shield") int32 GetHits() const { return Hits; }
	/** Hits / hits expected on the unshielded shape over the round so far. */
	UFUNCTION(BlueprintPure, Category = "Rain Shield") float GetWetness() const;
	UFUNCTION(BlueprintPure, Category = "Rain Shield") int32 GetStars() const;
	/** Particles alive in the rain right now (GPU count, read back by Niagara). */
	UFUNCTION(BlueprintPure, Category = "Rain Shield") int32 GetLiveParticleCount() const;
	UFUNCTION(BlueprintPure, Category = "Rain Shield") FString GetLastSweepFile() const { return LastSweepFile; }

private:
	struct FPlaced
	{
		int32 Piece = 0;
		TArray<FIntVector> Cells;
	};

	FVector GridCenter() const;
	FVector CellCenter(const FIntVector& Cell) const;
	FVector Direction() const;
	FString DirectionName(int32 Index) const;
	TArray<FIntVector> GhostCells() const;
	bool IsFree(const TArray<FIntVector>& Cells) const;
	void ResetPiece(int32 Index);
	void UploadBoxes(bool bWithBlockers);
	void UploadRain();
	void RefreshCubes();
	void EndRound();
	float ExpectedHits(float Seconds) const;
	float StreamHalf() const;
	float StreamLength() const;
	void HandleInput(float DeltaSeconds);
	void UpdateCamera();
	void DrawPlan() const;
	void ShowHud() const;
	void TickSweep(float DeltaSeconds);
	void FinishSweepLevel();

	ERainShieldPhase Phase = ERainShieldPhase::Plan;
	int32 LevelIndex = 0;
	TSet<FIntVector> ProtectedSet;
	TArray<FPlaced> Placed;
	int32 CurrentPiece = 0;
	TArray<FIntVector> PieceCells;
	FIntVector PieceAnchor = FIntVector::ZeroValue;
	int32 DirectionIndex = 0;
	TArray<FIntVector> Directions;
	float RoundClock = 0.0f;
	int32 Hits = 0;
	double HitWaterMl = 0.0;
	int32 MaxHitsPerCallback = 0;
	int32 SaturatedCallbacks = 0;
	int32 BoxesUsed = 0;
	int32 RainCount = 0;
	float ReferenceArea = 0.0f;
	float MeanFallSpeed = 700.0f;
	TArray<TPair<FVector, float>> RecentHits;

	UPROPERTY(Transient) TObjectPtr<ACineCameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<class APointLight> Backlight;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ProtectedMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BlockerMaterial;
	bool bViewSet = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	bool bOrbiting = false;

	// Sweep (rain.game.Sweep).
	TArray<int32> SweepCounts;
	int32 SweepIndex = INDEX_NONE;
	bool bSweepMeasuring = false;
	float SweepTimer = 0.0f;
	float SweepSettle = 3.0f;
	float SweepMeasure = 5.0f;
	int32 SweepFrames = 0;
	double SweepFrameMs = 0.0, SweepFrameMsMax = 0.0, SweepGameMs = 0.0, SweepRenderMs = 0.0, SweepGpuMs = 0.0;
	int32 SweepHitsAtStart = 0;
	FString SweepCsv;
	FString LastSweepFile;
	TSharedPtr<FParticlePerfStatsListener_GatherAll, ESPMode::ThreadSafe> SweepListener;
};
