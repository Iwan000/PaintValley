// =====================================================================================
// Project Rain - Paint Valley, System 2 (paint balls): neighbours (custom HLSL module, CPU VectorVM, Map in / MapOut,
// and Reader: a Particle Attribute Reader bound to this emitter, which reads the previous frame's balls by index).
//
// The neighbour query: each ball keeps the indices of its two nearest balls (Particles.BallNear, -1 for none) and
// refines them every frame, an approximate nearest-neighbour search that needs no grid: it measures them again, the
// two each of them keeps (a neighbour's neighbour is likely near) and three balls picked at random, and keeps the two
// closest. Within a few frames a ball in a clump knows its true nearest neighbours. Only flying or resting balls
// take part. Then it collides with them: overlapping balls are pushed apart (each takes half) and their approaching
// speeds exchanged with restitution $(BALL_RESTITUTION). Branch-free, like the update.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
// The VectorVM's frac is x - trunc(x) (negative below 0); x - floor(x) is in [0, 1) on both targets.
#define PB_FRAC(X) ((X) - floor(X))
float NbSelf = float(ExecIndex());
float NbCount = float(User.RainGameCount);
float NbState = Particles.BallState;
float NbMe = step(0.5, NbState) * step(NbState, 2.5) * step(0.5, User.RainGameActive);
float3 NbP = Particles.Position;
float3 NbV = Particles.Velocity;
float NbN0 = Particles.BallNear.x;
float NbN1 = Particles.BallNear.y;
float NbContact = 2.0 * $(BALL_RADIUS);
bool NbOk;

// Measures ball C (an index as a float): its distance from this ball (far when it is not a flying or resting ball,
// or this ball itself).
#define NB_MEASURE(C, OutD) { float3 NbQ; float NbS; \
	Reader.GetPositionByIndex<Attribute="Position">(int(max(C, 0.0)), NbOk, NbQ); \
	Reader.GetFloatByIndex<Attribute="BallState">(int(max(C, 0.0)), NbOk, NbS); \
	float NbLive = step(0.0, C) * step(C, NbCount - 1.0) * step(0.5, NbS) * step(NbS, 2.5) * step(0.5, abs(C - NbSelf)); \
	OutD = lerp(1e8, length(NbQ - NbP), NbLive); }
// Offers ball C at distance D to the two nearest so far (no ball twice).
#define NB_OFFER(C, D) { float NbNew = step(0.5, abs(C - NbN0)) * step(0.5, abs(C - NbN1)) * step(D, 1e7); \
	float NbB0 = NbNew * step(D, NbD0); float NbB1 = NbNew * (1.0 - NbB0) * step(D, NbD1); \
	NbN1 = lerp(lerp(NbN1, C, NbB1), NbN0, NbB0); NbD1 = lerp(lerp(NbD1, D, NbB1), NbD0, NbB0); \
	NbN0 = lerp(NbN0, C, NbB0); NbD0 = lerp(NbD0, D, NbB0); }

// The two kept from last frame, measured again (dropped if gone).
float NbD0;
float NbD1;
NB_MEASURE(NbN0, NbD0)
NB_MEASURE(NbN1, NbD1)
NbN0 = lerp(-1.0, NbN0, step(NbD0, 1e7));
NbN1 = lerp(-1.0, NbN1, step(NbD1, 1e7));
// Order them.
float NbSwap = step(NbD1, NbD0);
float NbT = NbN0;
NbN0 = lerp(NbN0, NbN1, NbSwap);
NbN1 = lerp(NbN1, NbT, NbSwap);
NbT = NbD0;
NbD0 = lerp(NbD0, NbD1, NbSwap);
NbD1 = lerp(NbD1, NbT, NbSwap);

// Their neighbours.
float2 NbOf0;
float2 NbOf1;
Reader.GetVector2DByIndex<Attribute="BallNear">(int(max(NbN0, 0.0)), NbOk, NbOf0);
Reader.GetVector2DByIndex<Attribute="BallNear">(int(max(NbN1, 0.0)), NbOk, NbOf1);
NbOf0 = lerp(float2(-1.0, -1.0), NbOf0, step(0.0, NbN0));
NbOf1 = lerp(float2(-1.0, -1.0), NbOf1, step(0.0, NbN1));
float NbDa;
float NbDb;
float NbDc;
float NbDd;
NB_MEASURE(NbOf0.x, NbDa)
NB_OFFER(NbOf0.x, NbDa)
NB_MEASURE(NbOf0.y, NbDb)
NB_OFFER(NbOf0.y, NbDb)
NB_MEASURE(NbOf1.x, NbDc)
NB_OFFER(NbOf1.x, NbDc)
NB_MEASURE(NbOf1.y, NbDd)
NB_OFFER(NbOf1.y, NbDd)

// Three at random (a new draw every frame).
float3 NbRnd = PB_FRAC(sin(float3(NbSelf * 12.9898 + Engine.Time * 78.233, NbSelf * 39.346 + Engine.Time * 11.135, NbSelf * 73.156 + Engine.Time * 52.235)) * 43758.5453);
float3 NbPick = floor(NbRnd * NbCount);
float NbDe;
float NbDf;
float NbDg;
NB_MEASURE(NbPick.x, NbDe)
NB_OFFER(NbPick.x, NbDe)
NB_MEASURE(NbPick.y, NbDf)
NB_OFFER(NbPick.y, NbDf)
NB_MEASURE(NbPick.z, NbDg)
NB_OFFER(NbPick.z, NbDg)

// Collide with the two nearest: push apart, exchange the approaching speed.
#define NB_COLLIDE(C) { float3 NbQ; float3 NbVq; \
	Reader.GetPositionByIndex<Attribute="Position">(int(max(C, 0.0)), NbOk, NbQ); \
	Reader.GetVectorByIndex<Attribute="Velocity">(int(max(C, 0.0)), NbOk, NbVq); \
	float NbDist = length(NbP - NbQ); \
	float NbHit = NbMe * step(0.0, C) * step(NbDist, NbContact); \
	float3 NbAway = (NbP - NbQ) / max(NbDist, 1e-3); \
	float NbVn = dot(NbV - NbVq, NbAway); \
	NbP = NbP + NbAway * (0.5 * max(NbContact - NbDist, 0.0) * NbHit); \
	NbV = NbV - NbAway * (0.5 * (1.0 + $(BALL_RESTITUTION)) * min(NbVn, 0.0) * NbHit); }
NB_COLLIDE(NbN0)
NB_COLLIDE(NbN1)
#undef NB_MEASURE
#undef NB_OFFER
#undef NB_COLLIDE

Particles.Position = NbP;
Particles.Velocity = NbV;
Particles.BallNear = lerp(float2(-1.0, -1.0), float2(NbN0, NbN1), NbMe);
#undef PB_FRAC
