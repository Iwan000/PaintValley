// Project Rain - Paint Valley: the rain paints a Monument Valley structure. A floating island, the object to
// paint and pre-placed blockers are axis-aligned boxes (FPaintSurface: one procedural mesh, one paint atlas),
// seen through an orthographic isometric camera in front of a soft gradient backdrop. Step 1 of the rework:
// the look (the paint, the rain and the round come next).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraDataInterfaceExport.h"
#include "PaintSurface.h"
#include "RainPaintGame.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class FParticlePerfStatsListener_GatherAll;

class ACameraActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class EPaintPhase : uint8
{
	/** Aim the rain; the clock waits. */
	Ready,
	/** It rains paint; the clock runs until the coverage goal. */
	Painting,
	/** The goal is reached: the time is the score. */
	Done,
};

/** Monument Valley tones of one material (sRGB colours, as sampled from the references). */
USTRUCT(BlueprintType)
struct FPaintPalette
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Top = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Light = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Dark = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Bottom = FColor::White;
	/** The colour (sRGB) the faces blend towards at the top of the level's gradient range, by GradAmount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Grad = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float GradAmount = 0.0f;
	/** Faint floor tiles on tops below TileMaxZ (actor space): size and line width (cm), darkening (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileSize = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileLine = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileDarken = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float TileMaxZ = 5.0f;
	/** Tint of the occluded corners (sRGB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor AO = FColor(128, 110, 120);
	/** Openings: a rim band inside the outline (width in cm), then the hole: its far end (Opening) and the side it
	 *  shows where the far end is moved OpeningShift cm up and to the left (a porthole's barrel, a window's reveal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Opening = FColor(0x60, 0x48, 0x38);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor OpeningRim = FColor(0x60, 0x48, 0x38);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float OpeningRimWidth = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor OpeningBarrel = FColor(0x60, 0x48, 0x38);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float OpeningShift = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Edge = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float EdgeAmount = 0.0f;
};

USTRUCT(BlueprintType)
struct FPaintLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintPalette> Palettes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintBox> Boxes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintOpening> Openings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintArch> Arches;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintSpire> Spires;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") TArray<FPaintRibbon> Ribbons;
	/** Height range of the palettes' gradient (cm, actor space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D GradRange = FVector2D(0.0, 250.0);
	/** Below Fade.Y the structure fades towards the backdrop (fully at Fade.X, times FadeAmount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D FadeRange = FVector2D(-120.0, -40.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float FadeAmount = 0.0f;
	/** Backdrop: centre and edge of the radial gradient, a soft glow (screen x, y, radius, amount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor BackCenter = FColor(0xff, 0xe9, 0xdc);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor BackEdge = FColor(0xfd, 0xe2, 0xd2);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor BackGlow = FColor(0xf6, 0xba, 0x95);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector4 BackGlowPos = FVector4(0.12, 1.0, 0.45, 0.9);
	/** The soft shadow under the floating island on the backdrop: it hangs under the island's bottom corner nearest
	 *  the camera (ShadowAt: the bottom's centre, actor space; ShadowHalf: its half-size), ShadowSize its radii (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector ShadowAt = FVector(0.0, 0.0, -110.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D ShadowHalf = FVector2D(0.0, 0.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector2D ShadowSize = FVector2D(200.0, 45.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor ShadowColor = FColor(0xf4, 0xb0, 0x8c);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float ShadowAmount = 0.0f;
	/** Point the camera looks at (actor space) and the view's height (cm; the width follows the window). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FVector ViewTarget = FVector(0.0, 0.0, 70.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float ViewHeight = 700.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") FColor Ink = FColor(0x6a, 0x3c, 0xff);
	/** The time for three stars (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") float ParSeconds = 30.0f;
	/** Played with System 2 (paint balls that bounce) by default, else with the rain (Tab switches before a round). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint") bool bPaintBalls = false;
};

UCLASS()
class PROJECTRAIN_API ARainPaintGame : public AActor, public INiagaraParticleCallbackHandler
{
	GENERATED_BODY()

public:
	ARainPaintGame();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Export Particle Data calls this on the game thread with the drops the target took (a few frames late): the
	 *  small drops it consumed and the big drops that seed puddles. */
	virtual void ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data, UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset) override;

	UPROPERTY(VisibleAnywhere, Category = "Paint") TObjectPtr<UProceduralMeshComponent> Surfaces;
	UPROPERTY(VisibleAnywhere, Category = "Paint") TObjectPtr<UNiagaraComponent> Rain;
	UPROPERTY(EditAnywhere, Category = "Paint") TSoftObjectPtr<UNiagaraSystem> RainSystem;
	/** System 2: elastic paint balls (CPU meshes, rain.paint.Balls of them), launched along the same direction; they
	 *  bounce off the ground and the blockers and burst on the target. Only one system plays at a time (the other is
	 *  paused). */
	UPROPERTY(VisibleAnywhere, Category = "Paint") TObjectPtr<UNiagaraComponent> PaintBalls;
	UPROPERTY(EditAnywhere, Category = "Paint") TSoftObjectPtr<UNiagaraSystem> PaintBallSystem;
	/** The paint balls on the GPU (rain.paint.BallsGPU 1): the same HLSL, the CPU -> GPU port measured in the report. */
	UPROPERTY(EditAnywhere, Category = "Paint") TSoftObjectPtr<UNiagaraSystem> PaintBallSystemGPU;
	/** The pool's tuning below follows the cvars rain.paint.BigShare, rain.paint.Budget and rain.paint.BigSeed.
	 *  The pool (rain.paint.Count drops, spawned once): this share are big drops, the rest small ones. Big drops seed
	 *  puddles and are never used up; they fall in proportion to the small drops still left. A small drop is used up
	 *  only where it lands on paint (it feeds that puddle); on bare surface it goes back to the cloud. */
	UPROPERTY(EditAnywhere, Category = "Paint") float BigShare = 0.03f;
	/** Paint in the whole pool, as a multiple of what the coverage goal needs: each small drop carries this share of
	 *  it, so the count changes the cost, not the difficulty. A big drop seeds a puddle of BigSeedDrops small drops. */
	UPROPERTY(EditAnywhere, Category = "Paint") float PaintBudget = 2.0f;
	UPROPERTY(EditAnywhere, Category = "Paint") float BigSeedDrops = 60.0f;
	UPROPERTY(EditAnywhere, Category = "Paint") float RainSpeed = 700.0f;
	/** Share of the target's visible surface that ends the round. */
	UPROPERTY(EditAnywhere, Category = "Paint") float CoverageGoal = 0.9f;
	/** Steering rates (degrees per second): around the vertical and away from straight down. */
	UPROPERTY(EditAnywhere, Category = "Paint") float SteerYawRate = 100.0f;
	UPROPERTY(EditAnywhere, Category = "Paint") float SteerTiltRate = 70.0f;
	/** The trackball at the bottom right, a see-through globe whose poles are the rain's axis: radius as a share of the
	 *  view's height; seconds of a still press that freezes the rain; seconds for the rain to stop or start again. */
	UPROPERTY(EditAnywhere, Category = "Paint") float BallSize = 0.07f;
	UPROPERTY(EditAnywhere, Category = "Paint") float FreezePressSeconds = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Paint") float FreezeEaseSeconds = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialInterface> BackdropMaterial;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialInterface> BallMaterial;
	UPROPERTY(EditAnywhere, Category = "Paint") TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(EditAnywhere, Category = "Paint") TArray<FPaintLevel> Levels;

	/** Isometric view: the camera's pitch (true isometric: -35.26) and yaw (45 + 90 k, turned with Q / E). */
	UPROPERTY(EditAnywhere, Category = "Paint") float ViewPitch = -35.264f;
	UPROPERTY(EditAnywhere, Category = "Paint") float ViewYaw = 45.0f;

	UFUNCTION(BlueprintCallable, Category = "Paint") void StartLevel(int32 Index);
	/** Sets the view at once (Python, screenshots): yaw in degrees, height in cm (0 keeps the level's). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetView(float Yaw, float Height = 0.0f);
	UFUNCTION(BlueprintPure, Category = "Paint") FString Describe() const;
	/** Starts the rain and the clock (Enter). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void StartRound();
	/** Clears the paint, refills the pool and waits again (R). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void ResetRound();
	/** The rain's direction: Azimuth around the vertical, Tilt away from straight down (degrees, 0..180). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetDirection(float Azimuth, float Tilt);
	/** The trackball, as the mouse drives it (also from Python): press on it (from Python: at its centre), drag the
	 *  cursor by (Dx, Dy) pixels (screen axes, y down), release. Like a globe: the point of its surface under the
	 *  cursor stays under it, and the arrow turns with the ball. A still press freezes the rain. */
	UFUNCTION(BlueprintCallable, Category = "Paint") void BallPress();
	UFUNCTION(BlueprintCallable, Category = "Paint") void BallRoll(float Dx, float Dy);
	UFUNCTION(BlueprintCallable, Category = "Paint") void BallRelease();
	UFUNCTION(BlueprintPure, Category = "Paint") float GetCoverage() const { return Surface.TargetCoverage(); }
	UFUNCTION(BlueprintPure, Category = "Paint") float GetClock() const { return Clock; }
	UFUNCTION(BlueprintPure, Category = "Paint") EPaintPhase GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category = "Paint") bool IsOutOfRain() const { return bOutOfRain; }
	/** Small drops in the pool at the start of the round, and still left (used up only on paint). */
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetPoolSize() const { return PoolSize; }
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetPoolLeft() const { return FMath::Max(PoolSize - Consumed, 0); }
	/** Big drops falling now (BigShare of the small drops left). */
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetBigActive() const;
	UFUNCTION(BlueprintPure, Category = "Paint") float GetFreeze() const { return Freeze; }
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetRainCount() const { return RainCount; }
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetNumPuddles() const { return Surface.NumPuddles(); }
	/** Rain particles alive (the GPU count, read back by Niagara). */
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetLiveParticleCount() const;
	UFUNCTION(BlueprintPure, Category = "Paint") FString GetLevelName() const;
	UFUNCTION(BlueprintPure, Category = "Paint") float GetParSeconds() const;
	UFUNCTION(BlueprintPure, Category = "Paint") FVector GetDirection() const { return Direction(); }
	UFUNCTION(BlueprintPure, Category = "Paint") FLinearColor GetInkColor() const;
	/** The drops in the air: the paint look (false) or the film's water drops (true, a lens image of what is behind
	 *  them). Landed, both paint the same. Switched before a round (F, or the panel's button). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetFilmDrops(bool bFilm);
	/** Plays with System 2 (paint balls) or the rain; switching waits for a new round (Tab). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetPaintBallMode(bool bBalls);
	UFUNCTION(BlueprintPure, Category = "Paint") bool GetPaintBallMode() const { return bPaintBallMode; }
	/** Paint balls in the pool at the start of the round, and still left (used up where they burst on the target). */
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetPaintBallPool() const { return PaintBallPool; }
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetPaintBallsLeft() const { return FMath::Max(PaintBallPool - PaintBallsUsed, 0); }
	/** Paint balls alive in the CPU emitter (the whole pool: none is ever killed). */
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetLivePaintBallCount() const;
	/** A scripted ~88 s tour for the video (-PaintDemo on the command line; -PaintDemoQuit quits at the end). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void StartDemo();
	/** The demo's caption (empty outside the demo). */
	UFUNCTION(BlueprintPure, Category = "Paint") FString GetDemoCaption() const { return DemoCaption; }
	/** True when System 2 runs on the GPU (rain.paint.BallsGPU 1). */
	UFUNCTION(BlueprintPure, Category = "Paint") bool GetPaintBallsOnGpu() const { return bPaintBallsGpu; }
	/** Diagnostics: Max paint balls (spread over the pool) with their state, cycle, seeds and position. */
	UFUNCTION(BlueprintCallable, Category = "Paint") FString DebugPaintBalls(int32 Max = 20) const;
	UFUNCTION(BlueprintPure, Category = "Paint") bool GetFilmDrops() const { return bFilmDrops; }
	/** The panel's look button, as the HUD drew it (viewport pixels): a click there switches the look. */
	void SetLookButton(const FBox2D& Rect) { LookButton = Rect; }
	void SetSystemButton(const FBox2D& Rect) { SystemButton = Rect; }
	/** The level select: the panel's level button (a click or L opens the list) and the list's rows, as the HUD drew
	 *  them (viewport pixels; an invalid box hides one). Number keys 1-9 pick a level directly. */
	void SetLevelButton(const FBox2D& Rect) { LevelButton = Rect; }
	void SetLevelRow(int32 Index, const FBox2D& Rect);
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetLevelListOpen(bool bOpen);
	UFUNCTION(BlueprintPure, Category = "Paint") bool IsLevelListOpen() const { return bLevelListOpen; }
	/** A click at Screen (viewport pixels) on the level list: picks the row's level and closes the list (also from
	 *  Python, to test the rows the HUD reported). False when no row is there. */
	UFUNCTION(BlueprintCallable, Category = "Paint") bool ClickLevelList(FVector2D Screen);
	/** The centre of the list's row Index as the HUD drew it ((-1, -1) when hidden): for testing ClickLevelList. */
	UFUNCTION(BlueprintPure, Category = "Paint") FVector2D GetLevelRowCenter(int32 Index) const
	{
		return LevelRows.IsValidIndex(Index) && LevelRows[Index].bIsValid ? LevelRows[Index].GetCenter() : FVector2D(-1.0, -1.0);
	}
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetNumLevels() const { return Levels.Num(); }
	UFUNCTION(BlueprintPure, Category = "Paint") int32 GetLevelIndex() const { return LevelIndex; }
	/** Level Index's name, its system (true: paint balls) and par time, for the list. */
	FString GetLevelNameAt(int32 Index) const { return Levels.IsValidIndex(Index) ? Levels[Index].Name : FString(); }
	bool IsBallLevel(int32 Index) const { return Levels.IsValidIndex(Index) && Levels[Index].bPaintBalls; }
	float GetParSecondsAt(int32 Index) const { return Levels.IsValidIndex(Index) ? Levels[Index].ParSeconds : 0.0f; }
	/** The panel's sliders: 0 the pool's size (before a round; applied on release), 1 the drops' opacity (any time).
	 *  The HUD draws them where it reports them (viewport pixels; an invalid box hides one). */
	void SetSliderRect(int32 Slider, const FBox2D& Rect) { if (Slider >= 0 && Slider < 2) { SliderRects[Slider] = Rect; } }
	float GetSliderValue(int32 Slider) const;
	FString GetSliderLabel(int32 Slider) const;
	/** How opaque the drops in the air are, times their own opacity (rain.paint.DropOpacity, 0..2). */
	UFUNCTION(BlueprintCallable, Category = "Paint") void SetDropOpacity(float Opacity);

private:
	void BuildLevel();
	void SetupRain();
	void SetupPaintBalls();
	/** The direction, the stream and the level's boxes' state shared by both systems. */
	void UploadShared(UNiagaraComponent* System, bool bActive);
	/** Plays the chosen system, pauses the other. */
	void ApplySystemMode();
	void UploadRain();
	void UploadFullFaces();
	void UploadBoxes();
	FVector Direction() const;
	void SetDirectionVector(const FVector& D);
	void UpdateCamera();
	void PlaceBall();
	/** The ball under a screen point (pixels): its surface point facing the camera, world axes about its centre (a
	 *  unit vector; on the rim beyond its outline). Grab at a point, then drag to another. */
	FVector BallPoint(const FVector2D& Screen) const;
	void BallGrab(const FVector2D& Screen);
	void BallDragTo(const FVector2D& Screen);
	void HandleInput(float DeltaSeconds);

	// -PaintMeasure: settle 4 s, then average stat unit's raw times and the played system's Niagara times over 5 s,
	// log them and quit.
	void TickMeasure(float DeltaSeconds);
	float MeasureClock = -1.0f;
	int32 MeasureFrames = 0;
	double MeasureFrameMs = 0.0, MeasureGameMs = 0.0, MeasureRenderMs = 0.0, MeasureGpuMs = 0.0;
	TSharedPtr<FParticlePerfStatsListener_GatherAll, ESPMode::ThreadSafe> MeasureListener;

	// Demo (-PaintDemo): timed steps, a caption on the HUD, a player that aims where the paint is missing.
	struct FDemoStep
	{
		float At = 0.0f;
		TFunction<void()> Do;
	};
	TArray<FDemoStep> DemoSteps;
	int32 DemoNext = 0;
	float DemoClock = -1.0f;
	float PendingDemoDelay = -1.0f;
	bool bQuitAfterDemo = false;
	FString DemoCaption;
	void TickDemo(float DeltaSeconds);
	/** The demo's player (bDemoSeek): every few seconds it finds the target's barest face that the rain can reach (the
	 *  sides out of sight count more: it goes and looks), turns the view a quarter (as Q / E) when that face is out of
	 *  sight, then steers the rain there, never in one jump (as the trackball would). A face still filling up keeps the
	 *  rain once more; one that gets no paint while aimed at is left for the rest of the round. */
	bool bDemoSeek = false;
	float DemoSeekClock = 0.0f;
	float DemoSeekEpoch = -1.0f;
	int32 DemoSeekFace = INDEX_NONE;
	int32 DemoSeekPainted = 0;
	int32 DemoSeekHolds = 0;
	TSet<int32> DemoSeekSkip;
	bool bDemoAiming = false;
	float DemoAimDelay = 0.0f;
	float DemoAimAzimuth = 0.0f;
	float DemoAimTilt = 0.0f;
	void TickSeek(float DeltaSeconds);
	/** Picks the next face and the direction into it (false: nothing left that the rain can reach). */
	bool PickSeekTarget();

	/** r.AntiAliasingMethod before the game set FXAA (restored at EndPlay: the film scene keeps its own). */
	int32 PreviousAntiAliasing = -1;

	int32 LevelIndex = 0;
	FPaintSurface Surface;
	bool bFilmDrops = false;
	/** rain.paint.BigShare as last applied (BigShare is it clamped). */
	float BigShareSet = 0.03f;
	/** Seconds since the pool ran dry (the round ends when the paint has settled, or after 3 s). */
	float DryClock = 0.0f;
	/** rain.paint.PauseIdle as last applied. */
	bool PausedIdle = true;
	bool bPaintBallMode = false;
	/** rain.paint.BallsGPU as last applied. */
	bool bPaintBallsGpu = false;
	// System 2's pool: balls at the start, used up; the paint each one carries (cm^2); its round (as Epoch).
	int32 PaintBallCount = 0;
	int32 PaintBallPool = 0;
	int32 PaintBallsUsed = 0;
	float PaintBallArea = 1.0f;
	float PaintBallEpoch = 0.0f;
	FBox2D LookButton = FBox2D(ForceInit);
	FBox2D SystemButton = FBox2D(ForceInit);
	FBox2D LevelButton = FBox2D(ForceInit);
	TArray<FBox2D> LevelRows;
	bool bLevelListOpen = false;
	FBox2D SliderRects[2] = { FBox2D(ForceInit), FBox2D(ForceInit) };
	int32 DraggingSlider = INDEX_NONE;
	/** The pool slider's count while it is dragged (the pool is rebuilt when it is let go). */
	int32 PoolSliderCount = 0;
	float DropOpacity = 1.0f;
	EPaintPhase Phase = EPaintPhase::Ready;
	bool bOutOfRain = false;
	float Clock = 0.0f;
	float SteerAzimuth = 45.0f;
	float SteerTilt = 25.0f;

	// The pool: small drops at the start, used up; the paint each one carries (cm^2); where the paint came from.
	int32 PoolSize = 0;
	int32 Consumed = 0;
	float PaintPerDrop = 1.0f;
	int32 BigSeeds = 0;
	double PaintFromSmall = 0.0;
	double PaintFromBig = 0.0;
	int32 MaxHitsPerFrame = 0;
	int32 FullExports = 0;
	float Epoch = 0.0f;
	float FullFacesClock = 0.0f;
	int32 RainCount = 0;
	int32 BoxesUsed = 0;
	/** The stream around the level (world space): centre, disc radius, length. */
	FVector StreamCenter = FVector::ZeroVector;
	float StreamRadius = 300.0f;
	float TargetYaw = 45.0f;
	float ViewHeight = 700.0f;
	float ViewAspect = 0.0f;
	double BuildSeconds = 0.0;

	// The trackball: pressed on it, where and how long ago, how far the cursor went from there; the freeze.
	bool bBallHeld = false;
	/** Held with the mouse (let go when the button is up), not from a script. */
	bool bBallByMouse = false;
	float BallHeldSeconds = 0.0f;
	float BallMoved = 0.0f;
	bool bFreezeHeld = false;
	float Freeze = 0.0f;
	FVector2D BallPressAt = FVector2D::ZeroVector;
	/** The cursor (pixels) and the surface point under it (BallPoint) as of the last drag. */
	FVector2D BallCursor = FVector2D::ZeroVector;
	FVector BallGrabbed = FVector::ForwardVector;
	FVector2D BallScreen = FVector2D::ZeroVector;
	float BallScreenRadius = 0.0f;
	/** The ball's orientation (world): its local -Z pole is the rain's direction, as of BallSpinDir. */
	FQuat BallSpin = FQuat::Identity;
	FVector BallSpinDir = FVector::ZeroVector;

	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Backdrop;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Ball;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BallShaft;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BallTip;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> Atlas;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BackdropMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BallMID;
};
