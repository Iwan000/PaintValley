// =====================================================================================
// Project Rain - Rain Shield, System 2 (hail): update (custom HLSL module, Map in / MapOut). Runs on the CPU
// (VectorVM) or the GPU (the measured CPU -> GPU port). Branch-free: every condition is a 0/1 factor and
// the outcomes are blended with it, because the VectorVM cannot flatten branches that declare variables.
// Divisions and swizzle writes are written out per component where the VectorVM differs from the GPU.
//
// Few, heavy, stateful particles, where the rain is many and simple:
//   - Motion: gravity along the fall direction D with quadratic drag (terminal speed $(HAIL_SPEED) cm/s).
//   - Collision: the step is swept against the play grid's boxes grown by the stone's radius (a sphere against
//     a box, slab test). A stone bounces off the face it hits (restitution, friction): it is not absorbed, so
//     it can glance off a blocker's edge onto the protected shape.
//   - State: 0 falling, 1 bouncing, 2 resting (on a face, below $(HAIL_REST_SPEED) cm/s), 3 melting (after
//     $(HAIL_MELT_DELAY) s at rest it shrinks at $(HAIL_MELT_RATE) cm/s, then re-enters upwind). A resting stone
//     whose support is gone (a broken blocker) falls again.
//   - Events to gameplay: every impact faster than $(HAIL_EVENT_SPEED) cm/s while the round is scored raises
//     Particles.RainGameHit for that frame; Export Particle Data reads it back with the impact point, the
//     stone's radius and Particles.HailImpact = (box kind: 1 blocker / 2 protected, impact speed, mass in g).
//     The game damages the shape or the blocker it hit (RainShieldGame.cpp).
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
float HuDt = clamp(Engine.DeltaTime, 1e-5, 0.05);
float3 HuP = Particles.Position;
float3 HuV = Particles.Velocity;
float HuR = Particles.HailRadius;
float HuState = Particles.HailState;
float HuTimer = Particles.HailTimer;
float HuCycle = Particles.HailCycle;
float HuAngle = Particles.HailAngle;
float4 HuRnd = Particles.RainRand;

float3 HuD = User.RainGameDir;
float HuUpZ = step(abs(HuD.z), 0.99);
float3 HuU = normalize(cross(HuD, float3(1.0 - HuUpZ, 0.0, HuUpZ)));
float3 HuW = cross(HuD, HuU);
float3 HuC = User.RainGameCenter;
float HuH = User.RainGameHalf;
float HuL = User.RainGameLength;
float HuOn = step(0.5, User.RainGameActive);

float3 HuRel = HuP - HuC;
float HuS = dot(HuRel, HuD);
float HuA = dot(HuRel, HuU);
float HuB = dot(HuRel, HuW);
// Inside the box (past the upwind face): only these stones collide and show.
float HuIn = step(-0.5 * HuL, HuS);
// Fresh randoms for a re-entry (the stone's seed and how often it has re-entered).
float2 HuNew = frac(sin(float2(HuRnd.x * 917.3 + HuCycle * 7.31, HuRnd.y * 613.7 + HuCycle * 3.17)) * 43758.5453);

// ---- motion: gravity along D, quadratic drag.
float HuSpeed = length(HuV);
float3 HuVf = HuV + (HuD * 981.0 - HuV * (HuSpeed * 981.0 / ($(HAIL_SPEED) * $(HAIL_SPEED)))) * HuDt;
// No zero components before dividing: the VectorVM divides safely (x / 0 = 0, not infinity), which would make the
// slab of an axis the stone does not move along always "overlap". Plain scalars: a compound assignment to a
// swizzle (HuSeg.x += ...) had no effect in the VectorVM.
float3 HuSeg0 = HuVf * HuDt;
float HuSx = HuSeg0.x + 1e-3 * step(abs(HuSeg0.x), 1e-4);
float HuSy = HuSeg0.y + 1e-3 * step(abs(HuSeg0.y), 1e-4);
float HuSz = HuSeg0.z + 1e-3 * step(abs(HuSeg0.z), 1e-4);
float3 HuSeg = float3(HuSx, HuSy, HuSz);
float3 HuInv = float3(1.0 / HuSx, 1.0 / HuSy, 1.0 / HuSz);
float3 HuR3 = float3(HuR, HuR, HuR);

// ---- collision: the first grid box (grown by the radius) the step reaches, and the face it enters (the axis
// whose slab it entered last).
float HuHitT = 2.0;
float HuKind = 0.0;
float3 HuN = float3(0.0, 0.0, 1.0);
#define HB_BOX(Mn, Mx, K) { float3 HbT1 = (Mn - HuR3 - HuP) * HuInv; float3 HbT2 = (Mx + HuR3 - HuP) * HuInv; \
	float3 HbLo = min(HbT1, HbT2); float3 HbHi = max(HbT1, HbT2); \
	float HbEnter = max(max(HbLo.x, HbLo.y), HbLo.z); float HbExit = min(min(HbHi.x, HbHi.y), HbHi.z); \
	float HbHit = step(0.5, K) * HuIn * step(max(HbEnter, 0.0), HbExit) * step(HbEnter, 1.0) * step(HbEnter + 1e-6, HuHitT); \
	float HbX = step(HbLo.y, HbLo.x) * step(HbLo.z, HbLo.x); float HbY = (1.0 - HbX) * step(HbLo.z, HbLo.y); \
	HuHitT = lerp(HuHitT, HbEnter, HbHit); HuKind = lerp(HuKind, K, HbHit); \
	HuN = lerp(HuN, float3(-sign(HuSeg.x) * HbX, -sign(HuSeg.y) * HbY, -sign(HuSeg.z) * (1.0 - HbX) * (1.0 - HbY)), HbHit); }
$(GAME_BOX_TESTS_HAIL)
#undef HB_BOX
float HuHit = HuOn * step(0.5, HuKind);

// Bounce: back to the face it entered (a stone that sank in slightly while resting is pushed out), the normal
// speed reflected with restitution (none below the rest speed), the tangential speed slowed by friction.
float3 HuAt = HuP + HuSeg * min(HuHitT, 1.0);
float HuVn = dot(HuVf, HuN);
float HuImp = max(-HuVn, 0.0);
float3 HuVb = (HuVf - HuN * HuVn) * (1.0 - $(HAIL_FRICTION)) - HuN * min(HuVn, 0.0) * $(HAIL_RESTITUTION) * step($(HAIL_REST_SPEED), HuImp);
float HuSlow = step(length(HuVb), $(HAIL_REST_SPEED));
float HuEvent = HuHit * step($(HAIL_EVENT_SPEED), HuImp) * step(0.5, User.RainGameScoring);

// State: a hit makes it bouncing (1), or resting (2+) once slow; without a hit a resting stone falls again.
float HuStateHit = lerp(1.0, max(HuState, 2.0), HuSlow);
float HuStateFree = lerp(HuState, 1.0, step(1.5, HuState));
float HuNewState = lerp(HuStateFree, HuStateHit, HuHit);
float HuNewTimer = HuTimer * (1.0 - HuHit * HuSlow * step(HuState, 1.5));
float3 HuNewP = lerp(HuP + HuVf * HuDt, HuAt + HuN * 0.05, HuHit);
float3 HuNewV = lerp(HuVf, HuVb, HuHit);

// Resting -> melting -> gone.
float HuResting = step(1.5, HuNewState);
HuNewTimer += HuDt * HuResting;
float HuMelting = HuResting * step($(HAIL_MELT_DELAY), HuNewTimer);
HuNewState = lerp(HuNewState, 3.0, HuMelting);
float HuNewR = HuR - $(HAIL_MELT_RATE) * HuDt * HuMelting;
float HuNewAngle = HuAngle + HuDt * (2.0 + 5.0 * HuRnd.y) * step(HuNewState, 1.5);

// Blown out of the side: the stream is periodic across the face too.
float3 HuRel2 = HuNewP - HuC;
float HuS2 = dot(HuRel2, HuD);
float HuA2 = dot(HuRel2, HuU);
float HuB2 = dot(HuRel2, HuW);
HuA2 -= 2.0 * HuH * round(HuA2 / (2.0 * HuH));
HuB2 -= 2.0 * HuH * round(HuB2 / (2.0 * HuH));
HuNewP = HuC + HuU * HuA2 + HuW * HuB2 + HuD * HuS2;

// Out through the far face or melted: the slot re-enters one period back (upwind, hidden), at a new spot.
float HuReenter = HuOn * max(step(0.5 * HuL, HuS2), step(HuNewR, 0.25));
float3 HuReP = HuC + HuU * ((2.0 * HuNew.x - 1.0) * HuH) + HuW * ((2.0 * HuNew.y - 1.0) * HuH) + HuD * (min(HuS2, 0.5 * HuL) - HuL);
float HuFreshR = $(HAIL_R_MIN) + HuRnd.y * ($(HAIL_R_MAX) - $(HAIL_R_MIN));

// Between rounds every stone waits upwind of the face, hidden, keeping its slot.
float3 HuParkP = HuC + HuU * HuA + HuW * HuB + HuD * lerp(HuS, -0.5 * HuL - HuRnd.z * HuL, HuIn);

float3 HuOutP = lerp(HuParkP, lerp(HuNewP, HuReP, HuReenter), HuOn);
float3 HuOutV = lerp(HuD * $(HAIL_SPEED), lerp(HuNewV, HuD * $(HAIL_SPEED), HuReenter), HuOn);
float HuKeep = HuOn * (1.0 - HuReenter);
Particles.Position = HuOutP;
Particles.Velocity = HuOutV;
float HuOutR = lerp(lerp(HuFreshR, HuNewR, HuKeep), HuR, 1.0 - HuOn);
Particles.HailRadius = HuOutR;
Particles.HailState = HuNewState * HuKeep;
Particles.HailTimer = HuNewTimer * HuKeep;
Particles.HailCycle = HuCycle + HuReenter;
Particles.HailAngle = HuNewAngle;
Particles.RainGameHit = HuEvent > 0.5;
Particles.RainGameHitPos = lerp(Particles.RainGameHitPos, HuAt - HuN * HuR, HuEvent);
Particles.HailImpact = float3(HuKind, HuImp, 4.18879 * HuR * HuR * HuR * 0.917) * HuEvent;

// The mesh: the Engine sphere (radius 50) at the stone's radius, squashed its own way, tumbling while it flies.
// Hidden (zero scale) upwind of the face and between rounds.
float HuShow = HuOn * step(-0.5 * HuL, dot(HuOutP - HuC, HuD));
Particles.Scale = (HuOutR / 50.0) * float3(0.85 + 0.3 * HuRnd.x, 0.85 + 0.3 * HuRnd.z, 0.8 + 0.25 * HuRnd.w) * HuShow;
float3 HuAxis = normalize(float3(HuRnd.x - 0.5, HuRnd.z - 0.5, HuRnd.w - 0.4));
Particles.MeshOrientation = float4(HuAxis * sin(0.5 * HuNewAngle), cos(0.5 * HuNewAngle));
