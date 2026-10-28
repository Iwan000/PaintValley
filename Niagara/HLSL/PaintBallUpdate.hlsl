// =====================================================================================
// Project Rain - Paint Valley, System 2 (paint balls): update (custom HLSL module, CPU VectorVM, Map in / MapOut).
// Branch-free like the Rain Shield hail it grew from (HailUpdate.hlsl): every condition is a 0/1 factor and the
// outcomes are blended with it (the VectorVM cannot flatten branches that declare variables); divisions and swizzle
// writes are written out per component.
//
// Few, heavy, elastic balls of paint, where the rain is many and simple. A fixed pool, spawned once; each ball cycles
// through Particles.BallState:
//   0 cloud     hidden, waiting (up to User.RainBallWait s), then launched from a fresh spot upwind along the
//               player's direction D, while the pool has balls left (User.RainBallLeft > 0: a used ball is out for
//               good, so no further thinning is needed);
//   1 flying    pulled along D ($(BALL_PULL) cm/s^2, quadratic drag to $(BALL_TOP_SPEED) cm/s): turning the
//               direction bends the flights. The step is swept against the level's boxes grown by the ball's
//               radius (slab test, no tunnelling). Off the ground, a blocker or a full face it bounces (restitution
//               $(BALL_RESTITUTION), friction $(BALL_FRICTION)) and squashes; on a target face that still takes
//               paint (User.RainBox<i>Full, while the round is scored) it bursts:
//   2 resting   slower than $(BALL_REST_SPEED) cm/s on a face; after $(BALL_REST_TIME) s (or $(BALL_MAX_BOUNCES)
//               bounces, or leaving the stream's sphere) back to the cloud;
//   3 burst     raises Particles.RainGameHit for that frame (Export Particle Data reads back the contact point, the
//               box kind and the velocity: the game paints there and counts the ball used), flattens against the
//               face for $(BALL_BURST_TIME) s, then
//   4 used      hidden until the next round (a new User.RainBallEpoch brings every ball back to the cloud).
// User.RainFreeze (0..1) slows everything to a stop.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
// The VectorVM's frac is x - trunc(x) (negative below 0); x - floor(x) is in [0, 1) on both targets.
#define PB_FRAC(X) ((X) - floor(X))
float BuDt = clamp(Engine.DeltaTime, 1e-5, 0.05) * (1.0 - saturate(User.RainFreeze));
float3 BuP = Particles.Position;
float3 BuV = Particles.Velocity;
float4 BuRnd = Particles.RainRand;
float BuState = Particles.BallState;
float BuTimer = Particles.BallTimer;
float BuBounces = Particles.BallBounces;
float BuSquash = Particles.BallSquash;
float BuCycle = Particles.BallCycle;
float BuR = $(BALL_RADIUS) * (0.85 + 0.3 * BuRnd.z);

float3 BuD = User.RainGameDir;
float BuUpZ = step(abs(BuD.z), 0.99);
float3 BuU = normalize(cross(BuD, float3(1.0 - BuUpZ, 0.0, BuUpZ)));
float3 BuW = cross(BuD, BuU);
float3 BuC = User.RainGameCenter;
float BuH = User.RainGameHalf;
float BuL = User.RainGameLength;
float BuOn = step(0.5, User.RainGameActive);
float BuExit = sqrt(0.25 * BuL * BuL + BuH * BuH);

// A new round: every ball back to the cloud.
float BuNewRound = step(0.5, abs(Particles.BallEpoch - User.RainBallEpoch));
Particles.BallEpoch = User.RainBallEpoch;
BuState = BuState * (1.0 - BuNewRound);
BuTimer = lerp(BuTimer, BuRnd.x * User.RainBallWait, BuNewRound);
BuCycle += BuNewRound;

// Fresh randoms for this cycle: where it is launched from, how long it waits next.
float2 BuNew = PB_FRAC(sin(float2(BuRnd.x * 917.3 + BuCycle * 7.31, BuRnd.y * 613.7 + BuCycle * 3.17)) * 43758.5453);
float BuRad = BuH * sqrt(BuNew.x);
float BuAng = 6.2831853 * BuNew.y;
float3 BuLaunchP = BuC + (BuU * cos(BuAng) + BuW * sin(BuAng)) * BuRad - BuD * (0.5 * BuL);
float BuActive = step(1e-6, User.RainBallLeft);

// ---- the cloud: wait, then launch (or wait again if the pool has no room for this ball).
float BuInCloud = step(BuState, 0.5);
float BuCloudTimer = BuTimer - BuDt * BuOn;
float BuDue = BuInCloud * BuOn * step(BuCloudTimer, 0.0);
float BuLaunch = BuDue * BuActive;
float BuRewait = BuDue * (1.0 - BuActive);

// ---- flying or resting: pulled along D with drag, swept against the boxes.
float BuMoving = step(0.5, BuState) * step(BuState, 2.5) * BuOn;
float BuSpeed = length(BuV);
float3 BuVf = BuV + (BuD * $(BALL_PULL) - BuV * (BuSpeed * $(BALL_PULL) / ($(BALL_TOP_SPEED) * $(BALL_TOP_SPEED)))) * BuDt;
float3 BuSeg0 = BuVf * BuDt;
float BuSx = BuSeg0.x + 1e-3 * step(abs(BuSeg0.x), 1e-4);
float BuSy = BuSeg0.y + 1e-3 * step(abs(BuSeg0.y), 1e-4);
float BuSz = BuSeg0.z + 1e-3 * step(abs(BuSeg0.z), 1e-4);
float3 BuSeg = float3(BuSx, BuSy, BuSz);
float3 BuInv = float3(1.0 / BuSx, 1.0 / BuSy, 1.0 / BuSz);
float3 BuR3 = float3(BuR, BuR, BuR);
float BuHitT = 2.0;
float BuKind = 0.0;
float BuFull = 0.0;
float BuPush = 0.0;
float3 BuN = float3(0.0, 0.0, 1.0);
// A ball that starts the step inside a grown box (pushed there by a neighbour, or sunk while resting) leaves by the
// nearest face, whichever way it moves (tracing the step back could leave by the far face).
#define BB_BOX(Mn, Mx, K, F) { float3 BbT1 = (Mn - BuR3 - BuP) * BuInv; float3 BbT2 = (Mx + BuR3 - BuP) * BuInv; \
	float3 BbLo = min(BbT1, BbT2); float3 BbHi = max(BbT1, BbT2); \
	float BbEnter = max(max(BbLo.x, BbLo.y), BbLo.z); float BbExit = min(min(BbHi.x, BbHi.y), BbHi.z); \
	float BbHit = step(0.5, K) * step(max(BbEnter, 0.0), BbExit) * step(BbEnter, 1.0) * step(max(BbEnter, 0.0) + 1e-6, BuHitT); \
	float BbX = step(BbLo.y, BbLo.x) * step(BbLo.z, BbLo.x); float BbY = (1.0 - BbX) * step(BbLo.z, BbLo.y); \
	float3 BbNs = float3(-sign(BuSeg.x) * BbX, -sign(BuSeg.y) * BbY, -sign(BuSeg.z) * (1.0 - BbX) * (1.0 - BbY)); \
	float3 BbD0 = BuP - (Mn - BuR3); float3 BbD1 = (Mx + BuR3) - BuP; float3 BbDm = min(BbD0, BbD1); \
	float BbIx = step(BbDm.x, BbDm.y) * step(BbDm.x, BbDm.z); float BbIy = (1.0 - BbIx) * step(BbDm.y, BbDm.z); float BbIz = (1.0 - BbIx) * (1.0 - BbIy); \
	float3 BbNi = float3(BbIx * lerp(1.0, -1.0, step(BbD0.x, BbD1.x)), BbIy * lerp(1.0, -1.0, step(BbD0.y, BbD1.y)), BbIz * lerp(1.0, -1.0, step(BbD0.z, BbD1.z))); \
	float BbInside = step(BbEnter, 0.0); \
	BuHitT = lerp(BuHitT, max(BbEnter, 0.0), BbHit); BuKind = lerp(BuKind, K, BbHit); BuFull = lerp(BuFull, F, BbHit); \
	BuN = lerp(BuN, lerp(BbNs, BbNi, BbInside), BbHit); BuPush = lerp(BuPush, BbInside * dot(BbDm, float3(BbIx, BbIy, BbIz)), BbHit); }
$(BALL_BOX_TESTS)
#undef BB_BOX
float BuHit = BuMoving * step(0.5, BuKind);

// The face's bit in its box's full mask (+X 1, -X 2, +Y 4, -Y 8, +Z 16, -Z 32).
float BuBit = step(0.5, BuN.x) + 2.0 * step(BuN.x, -0.5) + 4.0 * step(0.5, BuN.y) + 8.0 * step(BuN.y, -0.5) + 16.0 * step(0.5, BuN.z) + 32.0 * step(BuN.z, -0.5);
BuBit = max(BuBit, 1.0);
float BuFaceFull = floor(BuFull / BuBit) - 2.0 * floor(BuFull / (2.0 * BuBit));
float BuBurst = BuHit * step(abs(BuKind - $(KIND_TARGET)), 0.5) * (1.0 - BuFaceFull) * step(0.5, User.RainGameScoring) * step(BuState, 1.5);
float BuBounce = BuHit * (1.0 - BuBurst);

// Bounce: back to the face it entered, the normal speed reflected with restitution (none below the rest speed),
// the tangential speed slowed by friction; a squash as hard as the impact.
float3 BuAt = BuP + BuSeg * min(BuHitT, 1.0) + BuN * BuPush;
float BuVn = dot(BuVf, BuN);
float BuImp = max(-BuVn, 0.0);
float3 BuVb = (BuVf - BuN * BuVn) * (1.0 - $(BALL_FRICTION)) - BuN * min(BuVn, 0.0) * $(BALL_RESTITUTION) * step($(BALL_REST_SPEED), BuImp);
float BuSlow = step(length(BuVb), $(BALL_REST_SPEED));
float BuFlyState = lerp(lerp(BuState, 1.0, step(1.5, BuState)), lerp(1.0, 2.0, BuSlow), BuBounce);
// A burst ball settles against the face (its flattened disc 1 cm off it); it keeps its velocity for the export,
// stopped from the next frame on.
float3 BuFlyP = lerp(lerp(BuP + BuVf * BuDt, BuAt + BuN * 0.05, BuHit), BuAt - BuN * (BuR - 1.0), BuBurst);
float3 BuFlyV = lerp(BuVf, BuVb, BuBounce);
float BuFlyBounces = BuBounces + BuBounce * step($(BALL_REST_SPEED), BuImp);
float BuFlyTimer = (BuTimer + BuDt) * step(1.5, BuFlyState);
float BuDone = max(max(step(1.5, BuFlyState) * step($(BALL_REST_TIME), BuFlyTimer), step($(BALL_MAX_BOUNCES), BuFlyBounces)),
	step(BuExit, length(BuFlyP - BuC)) * step(0.0, dot(BuFlyV, BuFlyP - BuC)));

// ---- burst: flattening on the face, then used up.
float BuInBurst = step(2.5, BuState) * step(BuState, 3.5);
float BuBurstTimer = BuTimer + BuDt;
float BuUsed = BuInBurst * step($(BALL_BURST_TIME), BuBurstTimer);

// ---- the next state.
float BuRecycle = BuMoving * (1.0 - BuBurst) * BuDone;
float BuState1 = BuInCloud * lerp(0.0, 1.0, BuLaunch)
	+ BuMoving * lerp(lerp(BuFlyState, 0.0, BuRecycle), 3.0, BuBurst)
	+ BuInBurst * lerp(3.0, 4.0, BuUsed)
	+ step(3.5, BuState) * 4.0
	+ step(0.5, BuState) * step(BuState, 2.5) * (1.0 - BuOn) * BuState;
float BuTimer1 = BuInCloud * lerp(lerp(BuCloudTimer, BuNew.x * User.RainBallWait, BuRewait), 0.0, BuLaunch)
	+ BuMoving * lerp(lerp(BuFlyTimer, BuNew.x * User.RainBallWait, BuRecycle), 0.0, BuBurst)
	+ BuInBurst * BuBurstTimer
	+ step(0.5, BuState) * step(BuState, 2.5) * (1.0 - BuOn) * BuTimer;
float3 BuP1 = lerp(lerp(BuP, BuLaunchP, BuLaunch), BuFlyP, BuMoving);
float3 BuV1 = lerp(lerp(BuV, BuD * $(BALL_LAUNCH_SPEED), BuLaunch), BuFlyV, BuMoving) * (1.0 - BuInBurst);
float BuBounces1 = lerp(lerp(BuBounces, 0.0, BuLaunch), BuFlyBounces, BuMoving);
float BuSquash1 = max(BuSquash * exp(-14.0 * BuDt), BuBounce * saturate(BuImp / 900.0) * 0.5) * (1.0 - BuLaunch);
float BuCycle1 = BuCycle + BuRewait + BuRecycle;

Particles.Position = BuP1;
Particles.Velocity = BuV1;
Particles.BallState = BuState1;
Particles.BallTimer = BuTimer1;
Particles.BallBounces = BuBounces1;
Particles.BallSquash = BuSquash1;
Particles.BallCycle = BuCycle1;
Particles.RainGameHit = BuBurst > 0.5;
Particles.RainGameHitPos = lerp(Particles.RainGameHitPos, BuAt - BuN * BuR, BuBurst);
Particles.PaintKind = lerp(Particles.PaintKind, BuKind, BuBurst);

// ---- the look: the Engine sphere (radius 50) at the ball's radius, stretched along its flight and squashed by
// impacts; bursting, it spreads flat on the face (its normal kept in the velocity until then); hidden otherwise.
float BuShowFly = step(0.5, BuState1) * step(BuState1, 2.5) * BuOn;
float BuShowBurst = step(2.5, BuState1) * step(BuState1, 3.5) * BuOn;
float BuSp1 = length(BuV1);
float3 BuDir = lerp(BuD, BuV1 / max(BuSp1, 1e-3), step(1.0, BuSp1));
// Bursting: the face's normal stands in for the motion (kept in the velocity's place).
float3 BuBurstN = lerp(Particles.BallNormal, BuN, BuBurst);
Particles.BallNormal = BuBurstN;
BuDir = lerp(BuDir, BuBurstN, BuShowBurst);
float BuStretch = clamp(BuSp1 / $(BALL_TOP_SPEED) * 0.3, 0.0, 0.3) - BuSquash1;
float BuSpread = saturate(BuTimer1 / $(BALL_BURST_TIME));
float3 BuFlyScale = float3(1.0 - 0.5 * BuStretch, 1.0 - 0.5 * BuStretch, 1.0 + BuStretch);
float3 BuBurstScale = float3(1.0 + 0.9 * BuSpread, 1.0 + 0.9 * BuSpread, 0.6 - 0.45 * BuSpread);
Particles.Scale = (BuR / 50.0) * (BuFlyScale * BuShowFly + BuBurstScale * BuShowBurst);
// Turn the mesh's Z to BuDir (half-way quaternion; straight down has no half-way: a half turn about X).
float BuCos = BuDir.z;
float3 BuAxis = float3(-BuDir.y, BuDir.x, 0.0);
float BuFlip = step(1.0 + BuCos, 1e-3);
float4 BuQ = float4(BuAxis.x, BuAxis.y, BuAxis.z, 1.0 + BuCos);
BuQ = BuQ / max(length(BuQ), 1e-6);
Particles.MeshOrientation = lerp(BuQ, float4(1.0, 0.0, 0.0, 0.0), BuFlip);
Particles.Color = float4(User.RainPaintInk.rgb, 1.0);
#undef PB_FRAC
