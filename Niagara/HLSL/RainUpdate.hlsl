// =====================================================================================
// Project Rain - particle update (custom HLSL module, Map in / MapOut; RainField.hlsl comes first).
//
// Layer 1 (realism): every drop relaxes toward its terminal velocity (Atlas et al. 1973) with the
//   physical relaxation time tau = v_t / g, so released drops re-accelerate like real water.
// Layer 2 (control): a per-drop "frozen" weight blends the natural target velocity with a
//   controlled velocity field (hover drift + lift + chaos swirl + hand). The stop happens by
//   relaxing velocity with a short time constant (User.RainStopTau ~ 0.07 s -> 6-8 frames @24fps,
//   matching the film), so streak length shrinks continuously instead of popping. The player's
//   push and brush act on top of both: wind for the falling rain, flight for the hanging drops.
//
// Recycling: drops never die. They live in the rain field's periodic window; falling drops that
// reach the ground re-enter at the cloud base, and drops lifted up to the park height are "parked"
// (hidden) and re-enter from the clouds when the rain resumes. Hidden drops get a zero-size sprite
// so the GPU never rasterizes them.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
// A hitch (seconds-long frame) must not teleport falling drops metres underground; it slows the sim instead.
float PrDt = clamp(Engine.DeltaTime, 1e-5, 0.1);
float PrTime = Engine.Time;

float3 PrP = Particles.Position;
float3 PrV = Particles.Velocity;
float PrDcm = Particles.RainDiameter;
float PrDmm = PrDcm * 10.0;
float4 PrRnd = Particles.RainRand;
float PrFrz = Particles.RainFrozen;
float PrState = Particles.RainState;
float PrTimer = Particles.RainTimer;
float3 PrDir = Particles.RainDir;

float PrGround = Engine.Owner.Position.z;
float PrTop = PrGround + $(HEIGHT);
// Lifted drops keep rising well above the cloud base and fade out before being parked.
float PrParkZ = PrGround + $(PARK_HEIGHT);

// ---------------------------------------------------------------- natural (layer 1)
float PrVt = max(0.0, 9.65 - 10.3 * exp(-0.6 * PrDmm)) * 100.0 * (0.94 + 0.12 * PrRnd.w);
float3 PrVNat = float3(0.0, 0.0, -PrVt) + User.RainWind;
float PrTauNat = max(PrVt / 981.0, 0.05);
// Gusts: the air the rain falls through is never still. A slowly drifting gust field (two cells per
// period of the field, so it stays periodic) plus each drop's own wobble tilt the streaks a little each
// way; small drops follow the air more than big ones.
float PrGk = 6.2831853 * 2.0 / PR_CELL;
float2 PrGust = float2(
	sin(PrP.y * PrGk + PrTime * 0.6) + 0.6 * sin(PrTime * (1.1 + 0.6 * PrRnd.x) + 6.2832 * PrRnd.y),
	sin(PrP.x * PrGk + PrTime * 0.45 + 1.7) + 0.6 * sin(PrTime * (0.9 + 0.6 * PrRnd.y) + 6.2832 * PrRnd.z));
PrVNat.xy += User.RainGust * PrGust * saturate(2.5 / max(PrDmm, 0.5));

// ---------------------------------------------------------------- freeze field (layer 2)
// The stop wave spreads out from the hand (its nearest periodic image).
float3 PrRel = PrP - User.RainStopCenter;
PrRel.xy = PR_NEAREST(PrRel.xy);
float PrDist = length(PrRel);
float PrStopR = User.RainStopRadius;
float PrFrzTarget = 0.0;
if (PrStopR > 0.0)
{
	PrFrzTarget = 1.0 - smoothstep(PrStopR - max(User.RainStopSoftness, 1.0), PrStopR, PrDist);
}
PrFrz = PrFrz + (PrFrzTarget - PrFrz) * (1.0 - exp(-PrDt * 30.0));

// ---------------------------------------------------------------- controlled velocity field
// Hover: slow "breathing" drift so frozen drops are alive, not dead-still (~1 cm/s).
float3 PrVCtl = User.RainHover * float3(
	sin(PrTime * (0.45 + 0.5 * PrRnd.x) + 6.2832 * PrRnd.y),
	sin(PrTime * (0.40 + 0.6 * PrRnd.y) + 6.2832 * PrRnd.z),
	0.7 * sin(PrTime * (0.50 + 0.4 * PrRnd.z) + 6.2832 * PrRnd.x));

// Lift ("make it go up"): each drop starts rising after a delay that grows with height and
// randomness, accelerating up and out of the frame.
float PrLiftT = User.RainLiftTime - (PrRnd.x * User.RainLiftSpread + saturate((PrP.z - PrGround) / $(HEIGHT)) * 0.35 * User.RainLiftSpread);
if (User.RainLiftTime > 0.0 && PrLiftT > 0.0)
{
	PrVCtl.z += min(User.RainLiftAccel * PrLiftT, User.RainLiftMaxSpeed) * (0.8 + 0.4 * PrRnd.w);
}

// Chaos ("go insane"): a swirl around the performer + a time-varying ABC flow (divergence free), both
// periodic with the field. The swirl dies out by 6.5 m (inside half a period); the ABC flow has three
// ~5 m cells per period, smaller than the cloud, so drops dart in all directions instead of the
// whole cloud drifting one way. (Scaling the ABC flow by distance makes it compressible near 2-5 m,
// a mild density drift over a chaos beat.)
float PrChaos = User.RainChaos;
if (PrChaos > 0.0)
{
	float2 PrRelH = PrRel.xy;
	float PrRH = max(length(PrRelH), 1.0);
	float3 PrTang = float3(-PrRelH.y, PrRelH.x, 0.0) / PrRH;
	float PrSwirl = (1.0 - smoothstep(250.0, 650.0, PrRH)) * (0.6 + 0.4 * sin(PrTime * 1.7 + PrRnd.y * 6.2832));
	float PrK = 6.2831853 * 3.0 / PR_CELL;
	float PrA = 1.0;
	float PrB = 0.8;
	float PrC = 0.6;
	float3 PrQ = PrP * PrK + float3(PrTime * 0.8, PrTime * 0.6, PrTime * 0.45);
	float3 PrAbc = float3(PrA * sin(PrQ.z) + PrC * cos(PrQ.y), PrB * sin(PrQ.x) + PrA * cos(PrQ.z), PrC * sin(PrQ.y) + PrB * cos(PrQ.x));
	// Each drop also darts on its own (slowly turning random heading): breaks up the filaments that
	// chaotic mixing draws, so the cloud reads as a swarm.
	float3 PrDart = float3(sin(PrTime * (1.1 + PrRnd.x) + 6.2832 * PrRnd.z), sin(PrTime * (0.9 + PrRnd.y) + 6.2832 * PrRnd.x),
		0.6 * sin(PrTime * (1.3 + PrRnd.z) + 6.2832 * PrRnd.y));
	// The storm is centred on the performer: full strength within 2 m of the hand, 15% beyond 5 m (where the
	// cameras stand; otherwise every drop at the lens streaks across the frame).
	float PrNear = 1.0 - 0.85 * smoothstep(200.0, 500.0, PrRH);
	PrVCtl += PrChaos * (User.RainChaosSwirl * PrSwirl * PrTang + PrNear * (User.RainChaosTurb * PrAbc + 160.0 * PrDart));
	// Keep the stirred drops between the ground and the cloud base (no drops exist beyond either).
	PrVCtl.z += PrChaos * 8.0 * max(PrGround + 15.0 - PrP.z, 0.0);
	if (User.RainLiftTime <= 0.0)
	{
		PrVCtl.z -= PrChaos * 8.0 * max(PrP.z - PrTop, 0.0);
	}
}

// Hand: drops near the performer's hand get dragged along with the gesture and pushed aside.
float3 PrHandRel = PrP - User.RainHandPos;
PrHandRel.xy = PR_NEAREST(PrHandRel.xy);
float PrHandD = length(PrHandRel);
float PrHandW = User.RainHandStrength * exp(-(PrHandD * PrHandD) / max(User.RainHandRadius * User.RainHandRadius, 1.0));
PrVCtl += PrHandW * (User.RainHandVel + PrHandRel / max(PrHandD, 1.0) * length(User.RainHandVel) * 0.35);

// ---------------------------------------------------------------- the player (layer 2)
// Push: one velocity for the whole field. Hanging drops follow it at their own pace (+-30%), so the
// cloud streams rather than sliding as one block; the falling rain takes it as wind.
float3 PrPush = User.RainPush * (0.7 + 0.6 * PrRnd.z);
// Brush: drops near the cursor's ray turn about the camera with the ray, so at any depth they follow
// the cursor on screen. Rotation about a point isn't periodic: only drops within view feel it.
float3 PrFromCam = PrP - PrCam;
float PrCamD = max(length(PrFromCam), 1.0);
float PrBrushCos = dot(PrFromCam / PrCamD, User.RainBrushDir);
float PrBrushW = User.RainBrushStrength * exp((PrBrushCos - 1.0) / max(User.RainBrushCone, 1e-5)) * PR_FAR_FADE(PrP);
float3 PrBrush = PrBrushW * cross(User.RainBrushSpin, PrFromCam);

// ---------------------------------------------------------------- integrate
float PrTauCtl = max(User.RainStopTau * (0.75 + 0.5 * PrRnd.y), 0.005);
float3 PrVTarget = lerp(PrVNat, PrVCtl, PrFrz) + PrPush + PrBrush;
float PrTau = lerp(PrTauNat, PrTauCtl, PrFrz);
PrV = PrVTarget + (PrV - PrVTarget) * exp(-PrDt / PrTau);

if (PrState < 0.5)
{
	PrP += PrV * PrDt;
}

// ---------------------------------------------------------------- recycling
uint PrSeed = asuint(PrRnd.x) ^ (asuint(PrTime) * 2654435761u) ^ asuint(PrP.x * 0.37 + PrP.y);
PR_NEXT(PrSeed) float PrN0 = PR_U01(PrSeed);
PR_NEXT(PrSeed) float PrN1 = PR_U01(PrSeed);

bool PrRaining = User.RainWrap > 0.5 && PrFrzTarget < 0.5;
if (PrState < 0.5)
{
	if (PrP.z > PrParkZ && PrV.z > 0.0)
	{
		// Lifted out of sight: park until the rain resumes.
		PrState = 1.0;
		PrTimer = 0.0;
		PrV = float3(0.0, 0.0, 0.0);
	}
	else if (PrP.z < PrGround)
	{
		if (PrRaining)
		{
			// Hit the ground: fall again from the cloud base, somewhere else in the field.
			PrP = float3(PrWinMin + PR_CELL * float2(PrN0, PrN1), PrP.z + (PrTop - PrGround));
		}
		else if (PrFrz > 0.5)
		{
			// A held drop that dipped to the floor skims over it instead of vanishing.
			PrP.z = PrGround + 1.0;
			PrV.z = max(PrV.z, 0.0);
		}
		else
		{
			PrState = 1.0;
			PrTimer = 0.0;
			PrV = float3(0.0, 0.0, 0.0);
		}
	}
}
else if (PrRaining)
{
	// Parked: re-enter from the clouds after a per-drop random delay so the rain "arrives".
	PrTimer += PrDt;
	if (PrTimer > PrRnd.z * User.RainRespawnSpread)
	{
		PrState = 0.0;
		PrFrz = 0.0;
		PrP = float3(PrWinMin + PR_CELL * float2(PrN0, PrN1), PrTop - PrRnd.y * 60.0);
		PrV = PrVNat;
	}
}
// The window follows the camera: fold every drop back into it (only drops at its edges move).
PrP.xy = PR_FOLD(PrP.xy);

// ---------------------------------------------------------------- render attributes
float PrSpeed = length(PrV);
if (PrSpeed > 2.0)
{
	PrDir = PrV / PrSpeed;
}
// Hanging beads read bigger in the film (big rain-machine drops, bloom): art-directable boost.
float PrW = PrDcm * User.RainSizeScale * lerp(1.0, User.RainFrozenSizeScale, PrFrz);
float PrL = PrSpeed * User.RainShutter + PrW;

float PrVis = (PrState < 0.5 ? 1.0 : 0.0) * (frac(PrRnd.w * 7.31 + PrRnd.x) < User.RainActiveFraction ? 1.0 : 0.0);
PrVis *= saturate((PrTop - PrP.z) / 120.0 + PrFrz);
PrVis *= saturate((PrParkZ - PrP.z) / 600.0);
PrVis *= PR_FAR_FADE(PrP);

// Drops brushing the lens become huge sorted quads (overdraw) and distract; fade them out.
float3 PrToCam = PrCam - PrP;
PrCamD = max(length(PrToCam), 1.0);
PrVis *= saturate((PrCamD - 30.0) / 30.0);
// Outside the view (a cone through the frame's corners for any aspect >= 1, plus a margin; drops within
// a metre are big enough to reach in from outside it): invisible.
float PrViewCos = cos(atan(User.RainViewTan * 1.42) + 0.05);
if (PrCamD > 100.0 && dot(PrToCam, User.RainCameraFwd) > -PrViewCos * PrCamD)
{
	PrVis = 0.0;
}

Particles.Position = PrP;
Particles.Velocity = PrV;
Particles.RainFrozen = PrFrz;
Particles.RainState = PrState;
Particles.RainTimer = PrTimer;
Particles.RainDir = PrDir;
// Nearly round drops: keep the sprite square-on to the camera (an aligned sprite seen along its
// axis, e.g. looking up into the frozen rain, would collapse to a line).
PrToCam /= PrCamD;
float3 PrPerp = PrDir - PrToCam * dot(PrDir, PrToCam);
if (dot(PrPerp, PrPerp) < 1e-6)
{
	PrPerp = cross(PrToCam, float3(1.0, 0.0, 0.0));
}
float PrRound = 1.0 - saturate((PrL / max(PrW, 1e-4) - 1.0) * 2.0);
Particles.SpriteAlignment = normalize(lerp(PrDir, normalize(PrPerp), PrRound));
// ---------------------------------------------------------------- screen-size LOD
// Drops narrower than a few pixels can't show their ball-lens image: light them once per particle
// here (forward-scattering phase towards each stage light + ambient) and let the material take a
// cheap path. Only resolved drops near the camera get the full per-pixel ball-lens shading.
// The phase is broad: light refracted straight through a water sphere leaves it deviated by 0-83 deg,
// nearly evenly up to ~30 deg, so exp((cos - 1) / 0.3) (68% at 30 deg, 43% at 45 deg), plus a faint
// isotropic share for the reflected light (User.RainSide).
float PrPixW = PrW * User.RainPixelScale / PrCamD;
float PrSimple = PrPixW < User.RainLodPixels ? 1.0 : 0.0;

// Particle-space depth of field: the camera's own lens (thin lens, object-space blur diameter
// b = A |d - s| / s, A = focal length / f-number; User.RainDofScale = 1 is the real lens). The drops render
// after the engine's DOF, so each drop is blurred here: drawn over its blur disc with its light spread
// over the disc (alpha / blur^2), so a defocused drop is always dimmer than in focus, never a bright
// bokeh ball. Resolved drops keep their ball-lens shading, smeared by the blur in the material (Dyn.z).
// No drop is drawn smaller than 1.5 px either: a sub-pixel sprite crawls and flickers under temporal
// AA, so its light is spread over a 1.5 px footprint instead (same energy, like the blur).
float PrCoC = User.RainAperture * abs(PrCamD - User.RainFocusDist) / max(User.RainFocusDist, 1.0) * User.RainDofScale;
float PrBlurW = max(sqrt(PrW * PrW + PrCoC * PrCoC), 1.5 * PrCamD / max(User.RainPixelScale, 1.0));
float PrBlur = PrBlurW / PrW;
float PrSoft = sqrt(max(PrBlurW * PrBlurW - PrW * PrW, 0.0)) / PrBlurW;
PrVis *= 1.0 / (PrBlur * PrBlur);
float PrGrow = PrBlurW - PrW;
float3 PrLum = User.RainAmbient;
if (PrVis > 0.002)
{
#define PR_LIGHT(LP, LC) { float3 PrLd = PrP - LP.xyz; float PrD2 = dot(PrLd, PrLd); float PrCos = dot(PrLd * rsqrt(max(PrD2, 1.0)), PrToCam); PrLum += LC.rgb * (1.0e6 / max(PrD2, LP.w * LP.w + 100.0)) * (User.RainForward * exp((PrCos - 1.0) / 0.3) + User.RainSide); }
	PR_LIGHT(User.RainL0P, User.RainL0C)
	PR_LIGHT(User.RainL1P, User.RainL1C)
	PR_LIGHT(User.RainL2P, User.RainL2C)
	PR_LIGHT(User.RainL3P, User.RainL3C)
	PR_LIGHT(User.RainL4P, User.RainL4C)
	PR_LIGHT(User.RainL5P, User.RainL5C)
#undef PR_LIGHT
}

// Invisible drops (out of view, parked, faded out, too defocused, at the lens) collapse to degenerate
// quads: they cost nothing in rasterization and tile binning. (Not culled with Particles.VisibilityTag:
// in UE 5.7 the GPU cull + depth sort then loses the nearest drops, the end of the sorted list.)
Particles.SpriteSize = PrVis > 0.002 ? float2(PrW + PrGrow, PrL + PrGrow) : float2(0.0, 0.0);
Particles.Color = float4(PrLum * (0.85 + 0.3 * PrRnd.y), PrVis);
// x: stretch L/W. y: shading path, 1 = cheap (per-particle light); -1..0 = full ball-lens shading,
// with -y = how much of the per-particle scattered light to add (1 at the switch to the cheap path, 0 from
// 4x the size threshold), so drops don't change brightness when they switch (the environment refracted
// by a resolved drop is darker than the forward-scattering estimate). z: soft (defocused) fraction of
// the disc radius. w: random.
float PrGlow = saturate(1.0 - (PrPixW - User.RainLodPixels) / (3.0 * max(User.RainLodPixels, 0.1)));
Particles.DynamicMaterialParameter = float4((PrL + PrGrow) / (PrW + PrGrow), PrSimple > 0.5 ? 1.0 : -PrGlow, PrSoft, PrRnd.x);

#undef PR_NEXT
#undef PR_U01
#undef PR_CELL
#undef PR_FOLD
#undef PR_NEAREST
#undef PR_FAR_FADE
