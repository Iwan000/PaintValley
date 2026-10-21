// =====================================================================================
// Project Rain - splash droplets (secondary drops thrown by an impact; RainField.hlsl comes first).
// Garg & Nayar EGSR 2007 measured ~73 secondary drops for a 2 mm drop on a wet surface, leaving at
// ~2.1 m/s and ~68 +- 12 deg elevation; the crown itself lasts 10-20 ms. We use fewer, slightly
// larger droplets per impact and a ballistic flight; they render with the raindrop material, so
// motion-blur streaks and lamp glints come for free.
// Splashes only happen while it actually rains: User.RainSplash (0..1) gates them, so the
// puddles go still the moment the rain is frozen (as in the film).
// =====================================================================================

float4 PsRnd = Particles.RainRand;
uint PsCluster = uint(PsRnd.x);
float PsTime = Engine.Time;
float PsGround = Engine.Owner.Position.z;

// Each cluster (impact) re-fires with its own period and phase.
float PsPeriod = lerp(0.45, 1.1, PsRnd.w);
float PsCycleF = PsTime / PsPeriod + PsRnd.w * 13.7;
uint PsCycle = uint(floor(PsCycleF));
float PsAge = frac(PsCycleF) * PsPeriod;

uint PsH = (PsCluster * 2654435761u) ^ (PsCycle * 2246822519u) ^ $(SEED)u;
PR_NEXT(PsH) float PsU0 = PR_U01(PsH);
PR_NEXT(PsH) float PsU1 = PR_U01(PsH);
PR_NEXT(PsH) float PsU2 = PR_U01(PsH);
PR_NEXT(PsH) float PsU3 = PR_U01(PsH);

// Impact point: uniform over the rain field (a fixed world point of the periodic field, folded into the
// window, so splashes stay put while the window follows the camera), or, for the stage rain's share of the
// impacts, around the stage like its drops.
float3 PsImpact = float3(PsRnd.w < $(STAGE_SPLASH_SHARE) ? PR_STAGE_XY(PsU0, PsU1) : PR_FOLD(PR_CELL * float2(PsU0, PsU1)), PsGround + 0.5);
float PsImpactSpeed = lerp(0.6, 1.25, PsU2);             // relative impact energy (drop size)
bool PsFires = PsU3 < User.RainSplash * $(DENSITY);

// Droplet launch: azimuth uniform, elevation 68 +- 12 deg, speed ~2 m/s scaled by the impact.
uint PsDH = asuint(PsRnd.y) ^ (PsCycle * 747796405u);
PR_NEXT(PsDH) float PsA = PR_U01(PsDH);
PR_NEXT(PsDH) float PsE = PR_U01(PsDH);
PR_NEXT(PsDH) float PsS = PR_U01(PsDH);
float PsAz = 6.2832 * PsA;
float PsEl = radians(68.0 + (PsE * 2.0 - 1.0) * 14.0);
float PsSpeed = lerp(120.0, 260.0, PsS) * PsImpactSpeed;
float3 PsV0 = PsSpeed * float3(cos(PsEl) * cos(PsAz), cos(PsEl) * sin(PsAz), sin(PsEl));
float3 PsG = float3(0.0, 0.0, -981.0);

float3 PsP = PsImpact + PsV0 * PsAge + 0.5 * PsG * PsAge * PsAge;
float3 PsV = PsV0 + PsG * PsAge;

float PsVis = (PsFires && PsP.z > PsGround && PsAge < 0.45) ? 1.0 : 0.0;
PsVis *= saturate(1.0 - PsAge / 0.45);

float PsW = Particles.RainDiameter * User.RainSizeScale;

PsVis *= PR_FAR_FADE(PsP);

float PsCamDist = length(PrCam - PsP);
PsVis *= saturate((PsCamDist - 45.0) / 70.0);
float3 PsToEye = (PrCam - PsP) / max(PsCamDist, 1.0);
// Streak: the motion across the line of sight (RainField.hlsl).
float3 PsVS = PR_ACROSS(PsV, PsToEye);
float PsL = length(PsVS) * User.RainShutter + PsW;
float3 PsAlign = dot(PsVS, PsVS) > 1e-6 ? normalize(PsVS) : normalize(cross(PsToEye, abs(PsToEye.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));

Particles.Position = PsP;
Particles.Velocity = PsV;
Particles.RainDir = PsV / max(length(PsV), 1.0);
Particles.SpriteAlignment = PsAlign;
// Idle droplets (between impacts, under the floor) are degenerate quads: nothing to rasterize.
Particles.SpriteSize = PsVis > 0.002 ? float2(PsW, PsL) : float2(0.0, 0.0);
float3 PsLum = User.RainAmbient;
PR_LAMPS(PsLum, PsP, PsToEye)

// Splash droplets are always tiny on screen: cheap per-particle lighting path, round (bead ratio 1).
Particles.Color = float4(PsLum, PsVis);
Particles.DynamicMaterialParameter = float4(PsL / PsW, 1.0, 0.0, PsRnd.y);
Particles.DynamicMaterialParameter1 = float4(1.0, 0.0, 0.0, 0.0);

#undef PR_NEXT
#undef PR_U01
#undef PR_CELL
#undef PR_FOLD
#undef PR_NEAREST
#undef PR_FAR_FADE
#undef PR_STAGE_XY
#undef PR_LAMP
#undef PR_LAMPS
#undef PR_ACROSS
