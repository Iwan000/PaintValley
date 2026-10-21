// Project Rain - drives the NS_RainMagic Niagara system: the trick (rain -> stop -> hover -> chaos ->
// lift -> resume), either from the scripted performance timeline or from the player's controls, plus
// the global strobe, the performer's hand, stage-light glints, the camera the rain field follows and
// the environment capture used for the drops' refraction.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainMagicController.generated.h"

class ANiagaraActor;
class ASceneCaptureCube;
class APostProcessVolume;
class UMaterialParameterCollection;

UENUM(BlueprintType)
enum class ERainBeat : uint8
{
	Raining,
	Stop,
	Hover,
	Chaos,
	Lift,
	Resume,
};

/** Who runs the trick: the scripted performance, or the player (any control key in PIE takes over). */
UENUM(BlueprintType)
enum class ERainControl : uint8
{
	Performance,
	Player,
};

/** The trick at one instant. Filled from the performance timeline or from the player's controls, then
 *  sent to the rain, the ground and the strobe in one place. */
USTRUCT(BlueprintType)
struct FRainTrickState
{
	GENERATED_BODY()

	/** The drops are held (stopped, hovering, stirred or lifted) instead of falling. */
	UPROPERTY(BlueprintReadOnly, Category = "Rain") bool bHeld = false;
	/** The stop wave spreads out from here at StopWaveSpeed. */
	UPROPERTY(BlueprintReadOnly, Category = "Rain") FVector StopCenter = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Rain") float StopRadius = 0.0f;
	/** 0..1: how wildly the held drops are stirred. */
	UPROPERTY(BlueprintReadOnly, Category = "Rain") float Chaos = 0.0f;
	/** Seconds since the drops started rising; < 0: no lift. */
	UPROPERTY(BlueprintReadOnly, Category = "Rain") float LiftTime = -1.0f;
	/** 1: the rain falls (ground ripples, splashes); 0: every drop hangs. */
	UPROPERTY(BlueprintReadOnly, Category = "Rain") float Raining = 1.0f;
	/** Image gain of the strobe (1: off). */
	UPROPERTY(BlueprintReadOnly, Category = "Rain") float Strobe = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Rain") FVector Hand = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Rain") float HandStrength = 0.0f;
};

UCLASS(Blueprintable)
class PROJECTRAIN_API ARainMagicController : public AActor
{
	GENERATED_BODY()

public:
	ARainMagicController();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bPreviewInEditor; }
	virtual void BeginPlay() override;
	virtual void PostRegisterAllComponents() override;

	/** Drives the preview in the level editor (called every frame by the editor module). */
	void EditorPreviewTick(float DeltaSeconds);

	// ------------------------------------------------------------------ references
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TObjectPtr<ANiagaraActor> RainActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TObjectPtr<ASceneCaptureCube> EnvCapture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TObjectPtr<UMaterialParameterCollection> Collection;

	/** Light actors whose highlights are drawn in every drop (first six are used). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TArray<TObjectPtr<AActor>> GlintLights;

	/** The magician. The hand is placed relative to this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TObjectPtr<AActor> Performer;

	/** Camera used as the view target in PIE / game (the film camera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TObjectPtr<AActor> ViewCamera;

	/** Unbound post-process volume used for the strobe (colour gain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|References")
	TObjectPtr<APostProcessVolume> StrobeVolume;

	// ------------------------------------------------------------------ performance
	/** Seconds into the performance. Animate this from Sequencer for deterministic renders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Rain|Performance")
	float PerformanceTime = 0.0f;

	/** Performance time at BeginPlay (renders start from here; MRQ then advances it frame-locked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Performance")
	float StartTime = 0.0f;

	/** Advance PerformanceTime with real time (off when Sequencer drives it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Performance")
	bool bAutoAdvance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Performance")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Performance")
	bool bPreviewInEditor = true;

	/** Keyboard/mouse control in PIE (see the on-screen help: H). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Performance")
	bool bInteractive = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float StopAt = 6.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float HoverAt = 6.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float ChaosAt = 16.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float ChaosEnd = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float LiftAt = 28.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float ResumeAt = 37.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float PerformanceLength = 46.0f;

	/** Speed of the stopping wave that spreads out from the hand (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float StopWaveSpeed = 18000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float StopWaveMaxRadius = 8000.0f;
	/** Drop deceleration time constant: 0.07 s stops rain in 6-8 frames at 24 fps (as in the film). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Beats") float StopTau = 0.07f;

	// ------------------------------------------------------------------ player controls
	/** Who runs the trick right now. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rain|Player")
	ERainControl Control = ERainControl::Performance;
	/** Push speed at full input (cm/s): wind for the falling rain, flight speed for the hanging drops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Player") float PushSpeed = 150.0f;
	/** Time constant of the push ramping up and down (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Player") float PushResponse = 0.35f;
	/** Angular radius of the mouse brush (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Player") float BrushRadiusDeg = 4.0f;
	/** Time constant with which the brush follows the cursor (s): smooths the stroke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Player") float BrushResponse = 0.05f;
	/** Seconds for the chaos to build up or settle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Player") float ChaosRamp = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Player") bool bShowHelp = true;

	// ------------------------------------------------------------------ strobe (film: 2 frames bright / 2 dark at 23.976 fps)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Strobe") bool bStrobe = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Strobe") float StrobeHz = 23.976f / 4.0f;
	/** Bright/dark ratio ~1.28 in the film. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Strobe") float StrobeRatio = 1.28f;

	// ------------------------------------------------------------------ look
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float EnvGain = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float GlintGain = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float StreakGain = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float DropExposure = 1.0f;
	/** Converts a light's intensity into "glint brightness at 10 m". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float GlintPerCandela = 0.0004f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float ShutterAngle = 180.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float FrameRate = 23.976f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") FVector Wind = FVector(40.0f, 0.0f, 0.0f);
	/** Gustiness of the air (cm/s): tilts each falling streak a little its own way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float Gust = 90.0f;
	/** Brightness of unresolved (sub-pixel) drops: forward scattering of the stage lights. Set against the night
	 *  backdrop: the drops show up mostly by what they refract, the lamps add a bright core. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float ScatterGain = 2.0f;
	/** Extra sky radiance above the horizon in the drops' ball lenses. 0: the night backdrop's own sky (captured into
	 *  the drops' environment) is all they see; only a stage without a lit sky needs this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") FLinearColor SkyGlow = FLinearColor(0.0f, 0.0f, 0.0f);
	/** Ambient radiance of unresolved drops (night sky + city glow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") FLinearColor AmbientDrop = FLinearColor(0.006f, 0.0065f, 0.0075f);
	/** Hanging drops are drawn this much bigger than physical (the film's beads read large). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float FrozenSizeScale = 2.5f;
	/** >0 forces the pixels-per-radian used for drop LOD (e.g. for offline renders at a fixed resolution). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float PixelScaleOverride = 0.0f;
	/** >0: the output image width used for drop LOD (offline renders use a different size than the PIE window). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float RenderWidthOverride = 0.0f;
	/** Particle-space depth of field strength. 1 = the camera's physical thin lens (focal length, f-number, focus
	 *  distance): drops off the focal plane blur and dim (their light spread over the blur disc). 0 = pinhole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Look") float DofScale = 1.0f;

	// ------------------------------------------------------------------ API
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Rain")
	void CaptureEnvironment();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Rain")
	void RestartPerformance();

	/** Plays the scripted performance from a beat (hands the trick back from the player). */
	UFUNCTION(BlueprintCallable, Category = "Rain")
	void JumpToBeat(ERainBeat Beat);

	UFUNCTION(BlueprintPure, Category = "Rain")
	ERainBeat GetBeat() const;

	UFUNCTION(BlueprintPure, Category = "Rain")
	FRainTrickState GetTrickState() const { return State; }

	// The player's controls (keys in PIE; callable from Blueprint/Python too). Each takes over the
	// trick from the performance, starting from whatever the performance was doing.
	/** Stop the rain in mid-air (a stop wave from the hand) or let it fall again. */
	UFUNCTION(BlueprintCallable, Category = "Rain|Player")
	void SetHeld(bool bHold);

	UFUNCTION(BlueprintCallable, Category = "Rain|Player")
	void SetChaos(bool bOn);

	/** The held drops rise out of sight (holds the rain first if it is falling). */
	UFUNCTION(BlueprintCallable, Category = "Rain|Player")
	void StartLift();

	/** Push input relative to the view, each axis -1..1: X forward, Y right, Z up. */
	UFUNCTION(BlueprintCallable, Category = "Rain|Player")
	void SetPushInput(FVector Input);

	/** The mouse brush: while down, drops near this world-space ray from the camera follow it as it turns. */
	UFUNCTION(BlueprintCallable, Category = "Rain|Player")
	void SetBrush(bool bDown, FVector RayDirection);

	/** Hands the trick to the player, continuing from the performance's current state. */
	UFUNCTION(BlueprintCallable, Category = "Rain|Player")
	void TakeControl();

	/** Optional externally driven hand (e.g. from a tracked bone). Valid while bHandOverride. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Hand") bool bHandOverride = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Hand") FVector HandOverrideLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Hand") float HandRadius = 60.0f;
	/** Added to the performer's yaw to get the way it faces (UE mannequin meshes face local +Y: 90). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Hand") float PerformerYawOffset = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Hand") float HoverHandStrength = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain|Hand") float ChaosHandStrength = 2.0f;

private:
	/** The camera the rain field follows and its lens. */
	struct FRainView
	{
		FVector Location = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		float TanHalfFov = 1.0f;
		float PixelScale = 1000.0f;
	};
	FRainView GetView() const;
	class UCineCameraComponent* GetActiveCineCamera() const;

	void StepPerformance(float DeltaSeconds);
	void EnsureRainTicksLast();
	void HandleInteractiveInput();
	void ShowHelp() const;
	FRainTrickState PerformanceState(float T) const;
	FRainTrickState PlayerState() const;
	void StepPlayer(float DeltaSeconds, const FRainView& View);
	void ApplyState(float DeltaSeconds, const FRainView& View);
	void PushLightsAndCamera(const FRainView& View);
	FVector ScriptedHand(float T) const;
	float StrobeGain(float SinceStop) const;

	FRainTrickState State;
	FVector LastHand = FVector::ZeroVector;
	bool bHasLastHand = false;
	bool bPaused = false;
	float EnvCaptureCountdown = 0.5f;
	uint64 LastTickFrame = 0;

	// Player state (Control == Player).
	bool bPlayerHeld = false;
	float PlayerClock = 1000.0f;     // seconds since the drops were last held or released
	bool bPlayerChaos = false;
	float PlayerChaos = 0.0f;
	float PlayerLiftTime = -1.0f;
	FVector PushInput = FVector::ZeroVector;
	FVector Push = FVector::ZeroVector;
	bool bBrushDown = false;
	FVector LastKeyPush = FVector::ZeroVector;
	bool bLastMouseDown = false;
	FVector BrushTarget = FVector::ForwardVector;
	FVector BrushDir = FVector::ForwardVector;
	FVector BrushSpin = FVector::ZeroVector;
	float BrushStrength = 0.0f;
};
