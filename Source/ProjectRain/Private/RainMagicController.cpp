#include "RainMagicController.h"

#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/SceneCaptureCube.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif

ARainMagicController::ARainMagicController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// After the camera manager has updated (it runs after all other tick groups but this one): the view
	// read here is the one rendered this frame. Earlier in the frame, Movie Render Queue's sequence can
	// still be a frame ahead (it seeks back before rendering), which put the rain field in the next shot's
	// place on the last frame before every cut. The rain system ticks after this (see EnsureRainTicksLast).
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ARainMagicController::EnsureRainTicksLast()
{
	// The rain must simulate with this frame's camera and trick state, which are set in TG_PostUpdateWork.
	if (UNiagaraComponent* NC = RainActor ? RainActor->GetNiagaraComponent() : nullptr)
	{
		NC->SetTickBehavior(ENiagaraTickBehavior::ForceTickLast);
	}
}

void ARainMagicController::BeginPlay()
{
	Super::BeginPlay();
	EnsureRainTicksLast();
	EnvCaptureCountdown = 0.5f;
	bHasLastHand = false;
	PerformanceTime = StartTime;
	bAutoAdvance = true;
	Control = ERainControl::Performance;
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (ViewCamera)
		{
			PC->SetViewTarget(ViewCamera);
		}
		if (bInteractive)
		{
			PC->bShowMouseCursor = true;
			PC->SetInputMode(FInputModeGameAndUI());
		}
	}
}

void ARainMagicController::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// Editor worlds never call BeginPlay, so the actor tick is never registered there. Register it
	// ourselves so the performance previews live in the level editor.
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld() && bPreviewInEditor && !PrimaryActorTick.IsTickFunctionRegistered())
	{
		RegisterAllActorTickFunctions(true, false);
		EnvCaptureCountdown = 0.5f;
	}
}

void ARainMagicController::RestartPerformance()
{
	// Only the timeline restarts. The drops are never respawned: at t = 0 the held drops are released
	// and the parked ones fall back in from the sky. (Resetting the Niagara system here re-fired the
	// spawn burst without clearing the GPU particles, so every loop added another full set of drops.)
	PerformanceTime = 0.0f;
	bHasLastHand = false;
}

void ARainMagicController::JumpToBeat(ERainBeat Beat)
{
	Control = ERainControl::Performance;
	switch (Beat)
	{
	case ERainBeat::Raining: RestartPerformance(); break;
	case ERainBeat::Stop: PerformanceTime = StopAt - 0.05f; break;
	case ERainBeat::Hover: PerformanceTime = HoverAt; break;
	case ERainBeat::Chaos: PerformanceTime = ChaosAt; break;
	case ERainBeat::Lift: PerformanceTime = LiftAt; break;
	case ERainBeat::Resume: PerformanceTime = ResumeAt; break;
	}
}

ERainBeat ARainMagicController::GetBeat() const
{
	const float T = PerformanceTime;
	if (T < StopAt) return ERainBeat::Raining;
	if (T < HoverAt) return ERainBeat::Stop;
	if (T < ChaosAt) return ERainBeat::Hover;
	if (T < ChaosEnd) return ERainBeat::Chaos;
	if (T < ResumeAt) return T < LiftAt ? ERainBeat::Hover : ERainBeat::Lift;
	return ERainBeat::Resume;
}

void ARainMagicController::CaptureEnvironment()
{
	if (!EnvCapture)
	{
		return;
	}
	USceneCaptureComponentCube* Capture = EnvCapture->GetCaptureComponentCube();
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	if (RainActor)
	{
		Capture->HiddenActors.AddUnique(RainActor);
	}
	Capture->CaptureScene();
}

UCineCameraComponent* ARainMagicController::GetActiveCineCamera() const
{
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			if (AActor* Target = PCM->GetViewTarget())
			{
				if (UCineCameraComponent* Cine = Target->FindComponentByClass<UCineCameraComponent>())
				{
					return Cine;
				}
			}
		}
	}
	return ViewCamera ? ViewCamera->FindComponentByClass<UCineCameraComponent>() : nullptr;
}

#if WITH_EDITOR
// The level viewport the editor preview is seen through. GCurrentLevelEditingViewportClient is briefly
// unset (or an orthographic view) after the viewport camera is moved from script; fall back to the
// first perspective level viewport rather than to the controller's own transform.
static const FLevelEditorViewportClient* GetEditorViewClient()
{
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsPerspective())
	{
		return GCurrentLevelEditingViewportClient;
	}
	if (GEditor)
	{
		for (const FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (Client && Client->IsPerspective() && Client->Viewport)
			{
				return Client;
			}
		}
	}
	return nullptr;
}
#endif

ARainMagicController::FRainView ARainMagicController::GetView() const
{
	FRainView View;
	View.Location = GetActorLocation();
	View.Forward = GetActorForwardVector();
	float FovDeg = 60.0f;
	float Width = 1920.0f;
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		// This ticks after the camera manager's update: its cached view is exactly what gets rendered.
		if (APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			const FMinimalViewInfo& POV = PCM->GetCameraCacheView();
			View.Location = POV.Location;
			View.Forward = POV.Rotation.Vector();
			FovDeg = POV.FOV;
		}
		if (GEngine && GEngine->GameViewport)
		{
			FVector2D Size;
			GEngine->GameViewport->GetViewportSize(Size);
			Width = FMath::Max(Size.X, 64.0);
		}
	}
#if WITH_EDITOR
	else if (const FLevelEditorViewportClient* Client = GetEditorViewClient())
	{
		View.Location = Client->GetViewLocation();
		View.Forward = Client->GetViewRotation().Vector();
		FovDeg = Client->ViewFOV;
		if (Client->Viewport)
		{
			Width = FMath::Max<float>(Client->Viewport->GetSizeXY().X, 64.0f);
		}
	}
#endif
	View.TanHalfFov = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FovDeg, 5.0f, 170.0f)) * 0.5f);
	if (RenderWidthOverride > 0.0f)
	{
		Width = RenderWidthOverride;
	}
	// Pixels per radian across the view width: (W/2) / tan(hfov/2).
	View.PixelScale = PixelScaleOverride > 0.0f ? PixelScaleOverride : (Width * 0.5f) / View.TanHalfFov;
	return View;
}

static float SmoothStep01(float A, float B, float X)
{
	const float T = FMath::Clamp((X - A) / FMath::Max(B - A, 1e-4f), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

FVector ARainMagicController::ScriptedHand(float T) const
{
	const FTransform Base = Performer ? Performer->GetActorTransform() : GetActorTransform();
	// Skeletal meshes like the UE mannequin face their local +Y: PerformerYawOffset turns the actor
	// rotation into the direction the performer actually faces.
	FRotator Facing = Base.Rotator();
	Facing.Yaw += PerformerYawOffset;
	Facing.Pitch = 0.0f;
	Facing.Roll = 0.0f;
	const FVector Fwd = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	const FVector Up = FVector::UpVector;
	const FVector Chest = Base.GetLocation() + Up * 140.0f;

	// Talking pose while it rains, the "claw" thrust forward at the stop, then gestures.
	FVector Offset = Fwd * 35.0f + Right * 20.0f;
	const float Claw = SmoothStep01(StopAt - 0.6f, StopAt, T);
	Offset = FMath::Lerp(Offset, Fwd * 62.0f + Right * 12.0f + Up * 15.0f, Claw);

	if (T > HoverAt + 1.0f && T < ChaosAt)
	{
		// Slow presenting sweep (figure eight) through the hanging drops.
		const float S = T - (HoverAt + 1.0f);
		const float W = SmoothStep01(0.0f, 1.0f, S) * (1.0f - SmoothStep01(ChaosAt - HoverAt - 2.0f, ChaosAt - HoverAt - 1.0f, S));
		Offset += W * (Right * FMath::Sin(S * 0.7f) * 70.0f + Up * FMath::Sin(S * 1.4f) * 25.0f + Fwd * 10.0f);
	}
	if (T >= ChaosAt && T < ChaosEnd)
	{
		// Big conducting circles.
		const float S = T - ChaosAt;
		const float W = SmoothStep01(0.0f, 0.8f, S) * (1.0f - SmoothStep01(ChaosEnd - ChaosAt - 0.8f, ChaosEnd - ChaosAt, S));
		Offset += W * (Right * FMath::Cos(S * 4.4f) * 85.0f + Up * FMath::Sin(S * 4.4f) * 70.0f + Fwd * (20.0f + 15.0f * FMath::Sin(S * 2.0f)));
	}
	if (T >= LiftAt - 1.0f && T < ResumeAt)
	{
		// Both arms up: "make it go up".
		const float W = SmoothStep01(LiftAt - 1.0f, LiftAt, T);
		Offset = FMath::Lerp(Offset, Fwd * 25.0f + Right * 15.0f + Up * 85.0f, W);
	}
	return Chest + Offset;
}

// ---------------------------------------------------------------------------------------------- state

FRainTrickState ARainMagicController::PerformanceState(float T) const
{
	FRainTrickState S;
	S.bHeld = T >= StopAt && T < ResumeAt;
	// The stop spreads out from where the hand was when the trick was triggered.
	S.StopCenter = ScriptedHand(StopAt);
	S.StopRadius = S.bHeld ? FMath::Min((T - StopAt) * StopWaveSpeed, StopWaveMaxRadius) : 0.0f;
	S.Chaos = SmoothStep01(ChaosAt, ChaosAt + 1.5f, T) * (1.0f - SmoothStep01(ChaosEnd - 1.5f, ChaosEnd, T));
	S.LiftTime = (T >= LiftAt && T < ResumeAt) ? T - LiftAt : -1.0f;
	S.Raining = 1.0f - SmoothStep01(StopAt, StopAt + 0.3f, T) * (1.0f - SmoothStep01(ResumeAt + 0.5f, ResumeAt + 2.5f, T));
	S.Hand = ScriptedHand(T);
	// Gentle nudge while presenting (drops part around the hand, they are not swept away); strong
	// only while conducting the chaos.
	S.HandStrength = S.bHeld ? HoverHandStrength + (ChaosHandStrength - HoverHandStrength) * S.Chaos : 0.0f;
	return S;
}

FRainTrickState ARainMagicController::PlayerState() const
{
	FRainTrickState S;
	S.bHeld = bPlayerHeld;
	S.StopCenter = ScriptedHand(StopAt);
	S.StopRadius = bPlayerHeld ? FMath::Min(PlayerClock * StopWaveSpeed, StopWaveMaxRadius) : 0.0f;
	S.Chaos = PlayerChaos;
	S.LiftTime = bPlayerHeld ? PlayerLiftTime : -1.0f;
	S.Raining = bPlayerHeld ? 1.0f - SmoothStep01(0.0f, 0.3f, PlayerClock) : SmoothStep01(0.5f, 2.5f, PlayerClock);
	// The magician holds the presenting pose; the player's hands are the push and the brush.
	S.Hand = ScriptedHand(HoverAt);
	S.HandStrength = 0.0f;
	return S;
}

void ARainMagicController::TakeControl()
{
	if (Control == ERainControl::Player)
	{
		return;
	}
	// Carry on from wherever the performance is.
	const FRainTrickState Now = PerformanceState(PerformanceTime);
	Control = ERainControl::Player;
	bPlayerHeld = Now.bHeld;
	PlayerClock = Now.bHeld ? PerformanceTime - StopAt : (PerformanceTime >= ResumeAt ? PerformanceTime - ResumeAt : 1000.0f);
	bPlayerChaos = Now.Chaos > 0.5f;
	PlayerChaos = Now.Chaos;
	PlayerLiftTime = Now.LiftTime;
}

void ARainMagicController::SetHeld(bool bHold)
{
	TakeControl();
	if (bHold == bPlayerHeld)
	{
		return;
	}
	bPlayerHeld = bHold;
	PlayerClock = 0.0f;
	if (!bHold)
	{
		// Released drops fall as rain again (parked ones return from the clouds); the next hold starts calm.
		bPlayerChaos = false;
		PlayerLiftTime = -1.0f;
	}
}

void ARainMagicController::SetChaos(bool bOn)
{
	TakeControl();
	if (bOn)
	{
		SetHeld(true);
	}
	bPlayerChaos = bOn;
}

void ARainMagicController::StartLift()
{
	TakeControl();
	SetHeld(true);
	PlayerLiftTime = 0.0f;
}

void ARainMagicController::SetPushInput(FVector Input)
{
	if (!Input.IsNearlyZero())
	{
		TakeControl();
	}
	PushInput = Input.GetClampedToMaxSize(1.0f);
}

void ARainMagicController::SetBrush(bool bDown, FVector RayDirection)
{
	if (bDown)
	{
		BrushTarget = RayDirection.GetSafeNormal(UE_SMALL_NUMBER, BrushTarget);
		if (!bBrushDown)
		{
			BrushDir = BrushTarget;   // a new stroke starts under the cursor, not with a sweep from the last one
		}
	}
	bBrushDown = bDown;
}

void ARainMagicController::StepPlayer(float DeltaSeconds, const FRainView& View)
{
	if (Control == ERainControl::Player)
	{
		PlayerClock += DeltaSeconds;
		if (PlayerLiftTime >= 0.0f)
		{
			PlayerLiftTime += DeltaSeconds;
		}
		PlayerChaos = FMath::FInterpConstantTo(PlayerChaos, bPlayerChaos ? 1.0f : 0.0f, DeltaSeconds, 1.0f / FMath::Max(ChaosRamp, 0.05f));
	}

	// Push: view-relative input (forward/right along the ground, up) -> one world velocity, ramped.
	FVector Fwd(View.Forward.X, View.Forward.Y, 0.0f);
	Fwd = Fwd.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Right(-Fwd.Y, Fwd.X, 0.0f);
	const FVector PushTarget = (Fwd * PushInput.X + Right * PushInput.Y + FVector::UpVector * PushInput.Z).GetClampedToMaxSize(1.0f) * PushSpeed;
	Push += (PushTarget - Push) * (1.0f - FMath::Exp(-DeltaSeconds / FMath::Max(PushResponse, 0.01f)));

	// Brush: the ray eases after the cursor; its angular velocity is what the drops under it follow.
	const FVector PrevDir = BrushDir;
	BrushDir = (BrushDir + (BrushTarget - BrushDir) * (1.0f - FMath::Exp(-DeltaSeconds / FMath::Max(BrushResponse, 1e-3f)))).GetSafeNormal(UE_SMALL_NUMBER, BrushTarget);
	BrushSpin = DeltaSeconds > 1e-4f ? (FVector::CrossProduct(PrevDir, BrushDir) / DeltaSeconds).GetClampedToMaxSize(8.0f) : FVector::ZeroVector;
	BrushStrength = bBrushDown ? 1.0f : 0.0f;
}

// ---------------------------------------------------------------------------------------------- input

void ARainMagicController::HandleInteractiveInput()
{
	UWorld* World = GetWorld();
	if (!bInteractive || !World || !World->IsGameWorld())
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	// 1-6: the scripted performance from a beat.
	if (PC->WasInputKeyJustPressed(EKeys::One)) JumpToBeat(ERainBeat::Raining);
	if (PC->WasInputKeyJustPressed(EKeys::Two)) JumpToBeat(ERainBeat::Stop);
	if (PC->WasInputKeyJustPressed(EKeys::Three)) JumpToBeat(ERainBeat::Hover);
	if (PC->WasInputKeyJustPressed(EKeys::Four)) JumpToBeat(ERainBeat::Chaos);
	if (PC->WasInputKeyJustPressed(EKeys::Five)) JumpToBeat(ERainBeat::Lift);
	if (PC->WasInputKeyJustPressed(EKeys::Six)) JumpToBeat(ERainBeat::Resume);
	if (PC->WasInputKeyJustPressed(EKeys::P)) bPaused = !bPaused;
	if (PC->WasInputKeyJustPressed(EKeys::H)) bShowHelp = !bShowHelp;
	// V: the next framing. The rain field and the lens follow the view camera.
	if (PC->WasInputKeyJustPressed(EKeys::V) && ShowcaseCameras.Num() > 0)
	{
		const int32 Index = ShowcaseCameras.Find(ViewCamera);
		if (AActor* Next = ShowcaseCameras[(Index + 1) % ShowcaseCameras.Num()])
		{
			ViewCamera = Next;
			PC->SetViewTarget(ViewCamera);
		}
	}

	// The player's trick.
	if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		TakeControl();
		SetHeld(!bPlayerHeld);
	}
	if (PC->WasInputKeyJustPressed(EKeys::C))
	{
		TakeControl();
		SetChaos(!bPlayerChaos);
	}
	if (PC->WasInputKeyJustPressed(EKeys::R))
	{
		StartLift();
	}
	// Keys and mouse only write the push and the brush when they change (or while the button is held),
	// so the same controls can also be driven from Blueprint or Python.
	auto Axis = [PC](const FKey& Plus, const FKey& Minus)
	{
		return (PC->IsInputKeyDown(Plus) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(Minus) ? 1.0f : 0.0f);
	};
	const FVector KeyPush(Axis(EKeys::W, EKeys::S), Axis(EKeys::D, EKeys::A), Axis(EKeys::E, EKeys::Q));
	if (KeyPush != LastKeyPush)
	{
		SetPushInput(KeyPush);
		LastKeyPush = KeyPush;
	}

	const bool bMouseDown = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (bMouseDown || bLastMouseDown)
	{
		FVector Origin, Dir;
		const bool bDown = bMouseDown && PC->DeprojectMousePositionToWorld(Origin, Dir);
		SetBrush(bDown, bDown ? Dir : BrushTarget);
	}
	bLastMouseDown = bMouseDown;
}

void ARainMagicController::ShowHelp() const
{
	if (!GEngine)
	{
		return;
	}
	static const TCHAR* BeatNames[] = { TEXT("raining"), TEXT("stop"), TEXT("hover"), TEXT("chaos"), TEXT("lift"), TEXT("resume") };
	FString Status;
	if (Control == ERainControl::Performance)
	{
		Status = FString::Printf(TEXT("Rain: performance %.1f s (%s)%s"), PerformanceTime, BeatNames[int32(GetBeat())], bPaused ? TEXT(", paused") : TEXT(""));
	}
	else
	{
		Status = FString::Printf(TEXT("Rain: player - %s%s%s"), bPlayerHeld ? TEXT("held") : TEXT("falling"),
			bPlayerChaos ? TEXT(", chaos") : TEXT(""), PlayerLiftTime >= 0.0f ? TEXT(", rising") : TEXT(""));
	}
	GEngine->AddOnScreenDebugMessage(0x5241494E01ull, 0.0f, FColor(200, 220, 255), Status);
	GEngine->AddOnScreenDebugMessage(0x5241494E02ull, 0.0f, FColor(150, 160, 180),
		TEXT("Space hold/release   WASD + Q/E push   LMB drag: brush   C chaos   R rise   1-6 performance   V camera   P pause   H hide"));
}

// ---------------------------------------------------------------------------------------------- output

void ARainMagicController::PushLightsAndCamera(const FRainView& View)
{
	UWorld* World = GetWorld();
	if (!World || !Collection)
	{
		return;
	}
	UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(Collection);
	if (!Inst)
	{
		return;
	}
	UNiagaraComponent* NC = RainActor ? RainActor->GetNiagaraComponent() : nullptr;
	const FVector Cam = View.Location;
	Inst->SetVectorParameterValue(TEXT("CamPos"), FLinearColor(Cam.X, Cam.Y, Cam.Z, 0.0f));
	Inst->SetVectorParameterValue(TEXT("Look"), FLinearColor(EnvGain, GlintGain, StreakGain, DropExposure));
	Inst->SetVectorParameterValue(TEXT("SkyGlow"), SkyGlow);
	for (int32 i = 0; i < 6; ++i)
	{
		// P: position, soft radius. C: colour * glint brightness, cos of the inner cone. D: spot axis, cos of the
		// outer cone (point lamps: no cone).
		FLinearColor P(0, 0, -100000, 50);
		FLinearColor C(0, 0, 0, -1);
		FLinearColor D(0, 0, 0, -2);
		if (GlintLights.IsValidIndex(i) && GlintLights[i])
		{
			if (ULightComponent* Light = GlintLights[i]->FindComponentByClass<ULightComponent>())
			{
				const FVector L = Light->GetComponentLocation();
				float Radius = 30.0f;
				if (const UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
				{
					Radius = FMath::Max(Point->SourceRadius, 5.0f);
				}
				P = FLinearColor(L.X, L.Y, L.Z, Radius);
				const float Bright = Light->bAffectsWorld && Light->IsVisible() ? Light->Intensity * GlintPerCandela : 0.0f;
				C = Light->GetLightColor() * Bright;
				C.A = -1.0f;
				if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
				{
					const FVector Axis = Spot->GetForwardVector();
					D = FLinearColor(Axis.X, Axis.Y, Axis.Z, FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle)));
					C.A = FMath::Cos(FMath::DegreesToRadians(FMath::Min(Spot->InnerConeAngle, Spot->OuterConeAngle - 0.5f)));
				}
			}
		}
		Inst->SetVectorParameterValue(*FString::Printf(TEXT("L%dP"), i), P);
		Inst->SetVectorParameterValue(*FString::Printf(TEXT("L%dC"), i), C);
		Inst->SetVectorParameterValue(*FString::Printf(TEXT("L%dD"), i), D);
		if (NC)
		{
			NC->SetVariableVec4(*FString::Printf(TEXT("RainL%dP"), i), FVector4(P.R, P.G, P.B, P.A));
			NC->SetVariableVec4(*FString::Printf(TEXT("RainL%dC"), i), FVector4(C.R * ScatterGain, C.G * ScatterGain, C.B * ScatterGain, C.A));
			NC->SetVariableVec4(*FString::Printf(TEXT("RainL%dD"), i), FVector4(D.R, D.G, D.B, D.A));
		}
	}
	if (NC)
	{
		// The rain field's window follows this camera (Niagara/HLSL/RainField.hlsl).
		NC->SetVariableVec3(TEXT("RainCameraPos"), Cam);
		NC->SetVariableVec3(TEXT("RainCameraFwd"), View.Forward);
		NC->SetVariableFloat(TEXT("RainViewTan"), View.TanHalfFov);
		NC->SetVariableFloat(TEXT("RainPixelScale"), View.PixelScale);
		NC->SetVariableFloat(TEXT("RainFrozenSizeScale"), FrozenSizeScale);
		NC->SetVariableFloat(TEXT("RainLobe"), ScatterLobe);
		NC->SetVariableFloat(TEXT("RainDofScale"), DofScale);
		NC->SetVariableVec3(TEXT("RainAmbient"), FVector(AmbientDrop.R, AmbientDrop.G, AmbientDrop.B));
	}
	float BackdropBlur = 0.0f;
	float LensFocus = 1000.0f;
	if (UCineCameraComponent* Cine = GetActiveCineCamera())
	{
		// Aperture diameter in cm = focal length (mm) / f-number / 10. A cine camera that has not ticked
		// yet (e.g. just spawned in the editor) reports a focus distance of 0: use its manual focus.
		const float Focus = FMath::Max(Cine->CurrentFocusDistance > 0.0f ? Cine->CurrentFocusDistance : Cine->FocusSettings.ManualFocusDistance, 10.0f);
		const float Aperture = Cine->CurrentFocalLength / FMath::Max(Cine->CurrentAperture, 0.5f) / 10.0f;
		// Things at infinity blur to a disc of aperture / focus distance radians (the night backdrop).
		BackdropBlur = Aperture / Focus * DofScale;
		LensFocus = Focus;
		if (NC)
		{
			NC->SetVariableFloat(TEXT("RainFocusDist"), Focus);
			NC->SetVariableFloat(TEXT("RainAperture"), Aperture);
		}
	}
	// The same lens for the opaque stage (backdrop, pavement): Materials/HLSL/NightSky.hlsl, WetGround.hlsl.
	Inst->SetVectorParameterValue(TEXT("Lens"), FLinearColor(BackdropBlur, LensFocus, View.PixelScale, 0.0f));
}

void ARainMagicController::ApplyState(float DeltaSeconds, const FRainView& View)
{
	FVector Hand = bHandOverride ? HandOverrideLocation : State.Hand;
	FVector HandVel = FVector::ZeroVector;
	if (bHasLastHand && DeltaSeconds > 1e-4f)
	{
		HandVel = ((Hand - LastHand) / DeltaSeconds).GetClampedToMaxSize(1500.0f);
	}
	LastHand = Hand;
	bHasLastHand = true;

	if (UNiagaraComponent* NC = RainActor ? RainActor->GetNiagaraComponent() : nullptr)
	{
		NC->SetVariableVec3(TEXT("RainStopCenter"), State.StopCenter);
		NC->SetVariableFloat(TEXT("RainStopRadius"), State.StopRadius);
		NC->SetVariableFloat(TEXT("RainStopTau"), StopTau);
		NC->SetVariableFloat(TEXT("RainChaos"), State.Chaos);
		NC->SetVariableFloat(TEXT("RainLiftTime"), State.LiftTime);
		NC->SetVariableFloat(TEXT("RainWrap"), State.bHeld ? 0.0f : 1.0f);
		NC->SetVariableVec3(TEXT("RainHandPos"), Hand);
		NC->SetVariableVec3(TEXT("RainHandVel"), HandVel);
		NC->SetVariableFloat(TEXT("RainHandRadius"), HandRadius);
		NC->SetVariableFloat(TEXT("RainHandStrength"), State.HandStrength);
		NC->SetVariableVec3(TEXT("RainWind"), Wind);
		NC->SetVariableFloat(TEXT("RainGust"), Gust);
		NC->SetVariableFloat(TEXT("RainShutter"), (ShutterAngle / 360.0f) / FMath::Max(FrameRate, 1.0f));
		NC->SetVariableVec3(TEXT("RainPush"), Push);
		NC->SetVariableVec3(TEXT("RainBrushDir"), BrushDir);
		NC->SetVariableVec3(TEXT("RainBrushSpin"), BrushSpin);
		NC->SetVariableFloat(TEXT("RainBrushStrength"), BrushStrength);
		// exp((cos - 1) / c) falls to 1/e at the brush radius when c = r^2 / 2.
		NC->SetVariableFloat(TEXT("RainBrushCone"), 0.5f * FMath::Square(FMath::DegreesToRadians(BrushRadiusDeg)));
		// No splashes while the rain hangs in the air (the puddles go still, as in the film).
		NC->SetVariableFloat(TEXT("RainSplash"), State.Raining);
	}

	if (UWorld* World = GetWorld())
	{
		if (UMaterialParameterCollectionInstance* Inst = Collection ? World->GetParameterCollectionInstance(Collection) : nullptr)
		{
			Inst->SetScalarParameterValue(TEXT("RainIntensity"), State.Raining);
		}
	}
	PushLightsAndCamera(View);
}

// ---------------------------------------------------------------------------------------------- tick

void ARainMagicController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StepPerformance(DeltaSeconds);
}

void ARainMagicController::EditorPreviewTick(float DeltaSeconds)
{
	if (bPreviewInEditor)
	{
		StepPerformance(DeltaSeconds);
	}
}

void ARainMagicController::StepPerformance(float DeltaSeconds)
{
	if (LastTickFrame == GFrameCounter)
	{
		return;
	}
	LastTickFrame = GFrameCounter;
	HandleInteractiveInput();

	if (Control == ERainControl::Performance && bAutoAdvance && !bPaused)
	{
		PerformanceTime += DeltaSeconds;
		if (bLoop && PerformanceTime > PerformanceLength)
		{
			RestartPerformance();
		}
	}
	const FRainView View = GetView();
	StepPlayer(DeltaSeconds, View);
	State = Control == ERainControl::Performance ? PerformanceState(PerformanceTime) : PlayerState();
	ApplyState(DeltaSeconds, View);

	UWorld* World = GetWorld();
	if (bShowHelp && bInteractive && World && World->IsGameWorld())
	{
		ShowHelp();
	}
	if (EnvCaptureCountdown > 0.0f)
	{
		EnvCaptureCountdown -= DeltaSeconds;
		if (EnvCaptureCountdown <= 0.0f)
		{
			CaptureEnvironment();
		}
	}
}
