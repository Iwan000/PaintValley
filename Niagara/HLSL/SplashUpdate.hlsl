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

// Impact point: uniform over the rain field. It is a fixed world point of the periodic field, folded
// into the window, so splashes stay put while the window follows the camera.
float3 PsImpact = float3(PR_FOLD(PR_CELL * float2(PsU0, PsU1)), PsGround + 0.5);
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

float PsSpeedNow = length(PsV);
float3 PsDir = PsV / max(PsSpeedNow, 1.0);
float PsW = Particles.RainDiameter * User.RainSizeScale;
float PsL = PsSpeedNow * User.RainShutter + PsW;

PsVis *= PR_FAR_FADE(PsP);

float PsCamDist = length(PrCam - PsP);
PsVis *= saturate((PsCamDist - 45.0) / 70.0);

Particles.Position = PsP;
Particles.Velocity = PsV;
Particles.RainDir = PsDir;
Particles.SpriteAlignment = PsDir;
// Idle droplets (between impacts, under the floor) are degenerate quads: nothing to rasterize.
Particles.SpriteSize = PsVis > 0.002 ? float2(PsW, PsL) : float2(0.0, 0.0);
float3 PsToEye = (PrCam - PsP) / max(PsCamDist, 1.0);
float3 PsLum = User.RainAmbient;
#define PS_LIGHT(LP, LC) { float3 PsLd = PsP - LP.xyz; float PsD2 = dot(PsLd, PsLd); float PsCos = dot(PsLd * rsqrt(max(PsD2, 1.0)), PsToEye); PsLum += LC.rgb * (1.0e6 / max(PsD2, LP.w * LP.w + 100.0)) * (User.RainForward * exp((PsCos - 1.0) / 0.3) + User.RainSide); }
PS_LIGHT(User.RainL0P, User.RainL0C)
PS_LIGHT(User.RainL1P, User.RainL1C)
PS_LIGHT(User.RainL2P, User.RainL2C)
PS_LIGHT(User.RainL3P, User.RainL3C)
PS_LIGHT(User.RainL4P, User.RainL4C)
PS_LIGHT(User.RainL5P, User.RainL5C)
#undef PS_LIGHT

// Splash droplets are always tiny on screen: cheap per-particle lighting path.
Particles.Color = float4(PsLum, PsVis);
Particles.DynamicMaterialParameter = float4(PsL / PsW, 1.0, 0.0, PsRnd.y);

#undef PR_NEXT
#undef PR_U01
#undef PR_CELL
#undef PR_FOLD
#undef PR_NEAREST
#undef PR_FAR_FADE
