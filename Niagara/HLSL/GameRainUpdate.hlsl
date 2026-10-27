// =====================================================================================
// Project Rain - Rain Shield (the game): update (custom HLSL module, Map in / MapOut). The builder puts
// RainField.hlsl in front (hash, lamps) and RainLook.hlsl behind it: the same drop shape, streak, LOD, lens
// and lamp shading as the film rain, so the game's drops look like the trick's.
//
// Three rules per drop:
//   1. Fall along the player's direction D at the drop's terminal velocity (relaxation time v_t / g).
//   2. Gusts across D: an ABC flow (Arnold-Beltrami-Childress, divergence free) drifting in time, small
//      drops following the air more than big ones.
//   3. The grid: the step from last frame's position to this one is tested against the play grid's boxes
//      (slab test, so no drop tunnels through a one-cell plate). Blockers absorb the drop; the protected
//      shape is hit: while the round is scored the drop raises Particles.RainGameHit for one frame and the
//      Export Particle Data module behind this one reads it back to gameplay (hit point, size).
//
// Streaming: a drop is one slot of a periodic stream of length L along D. A drop that leaves the far face, is
// absorbed or hits the shape moves back by exactly L (to where its slot re-enters, upwind and hidden) with a
// new spot across the face. The flux through the face therefore never changes, whatever the blockers catch,
// and the expected number of hits on an unshielded shape is known (RainShieldGame.cpp).
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
float PrDt = clamp(Engine.DeltaTime, 1e-5, 0.1);
float PrTime = Engine.Time;

float3 PrP = Particles.Position;
float3 PrV = Particles.Velocity;
float PrDcm = Particles.RainDiameter;
float PrDmm = PrDcm * 10.0;
float4 PrRnd = Particles.RainRand;
float PrFrz = 0.0;
float PrState = 0.0;
float PrTimer = Particles.RainTimer;
float3 PrDir = Particles.RainDir;
float PrCamD = 1.0;

float3 GrD = User.RainGameDir;
float3 GrU = normalize(cross(GrD, abs(GrD.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 GrV = cross(GrD, GrU);
float3 GrC = User.RainGameCenter;
float GrH = User.RainGameHalf;
float GrL = User.RainGameLength;
bool GrActive = User.RainGameActive > 0.5;

float PrVt = max(0.0, 9.65 - 10.3 * exp(-0.6 * PrDmm)) * 100.0 * (0.94 + 0.12 * PrRnd.w);

// Where the drop is in the stream: along D (s) and across the face (a, b).
float3 GrRel = PrP - GrC;
float GrS = dot(GrRel, GrD);
float GrA = dot(GrRel, GrU);
float GrB = dot(GrRel, GrV);

// Fresh randoms for a re-entry: the drop's own seed and how many times it has re-entered (RainTimer).
uint GrSeed = uint(PrRnd.x * 16777215.0) * 2654435761u + uint(PrTimer) * 747796405u + $(SEED)u;
PR_NEXT(GrSeed) float GrR0 = PR_U01(GrSeed);
PR_NEXT(GrSeed) float GrR1 = PR_U01(GrSeed);

bool GrHit = false;
float3 GrHitPos = Particles.RainGameHitPos;
if (!GrActive)
{
	// Between rounds every drop waits upwind of the face, hidden, keeping its slot.
	if (GrS > -0.5 * GrL)
	{
		GrS = -0.5 * GrL - PrRnd.z * GrL;
	}
	PrV = GrD * PrVt;
	PrP = GrC + GrU * GrA + GrV * GrB + GrD * GrS;
}
else
{
	// ---- rule 1 + 2: fall along D through the gusts.
	float3 GrQ = PrP * (6.2831853 / $(GAME_GUST_WAVELENGTH)) + float3(0.0, 1.7, 3.1);
	float3 GrGust = float3(
		sin(GrQ.z + PrTime * 0.7) + cos(GrQ.y + PrTime * 0.3),
		sin(GrQ.x + PrTime * 0.5) + cos(GrQ.z + PrTime * 0.4),
		sin(GrQ.y + PrTime * 0.6) + cos(GrQ.x + PrTime * 0.35));
	GrGust -= GrD * dot(GrGust, GrD);
	float3 GrVTarget = GrD * PrVt + User.RainGameGust * GrGust * saturate(2.5 / max(PrDmm, 0.5));
	PrV += (GrVTarget - PrV) * (1.0 - exp(-PrDt / max(PrVt / 981.0, 0.05)));
	float3 GrP0 = PrP;
	float3 GrSeg = PrV * PrDt;
	PrP = GrP0 + GrSeg;

	// ---- rule 3: the first grid box this step crosses (only inside the face: parked drops never collide).
	float GrHitT = 2.0;
	float GrHitType = 0.0;
	if (GrS > -0.5 * GrL)
	{
		float3 GrInv = 1.0 / (GrSeg + 1e-4 * (1.0 - abs(sign(GrSeg))));
#define GR_BOX(Mn, Mx, K) if (K > 0.5) { float3 GrT1 = (Mn - GrP0) * GrInv; float3 GrT2 = (Mx - GrP0) * GrInv; \
	float3 GrLo = min(GrT1, GrT2); float3 GrHi = max(GrT1, GrT2); \
	float GrIn = max(max(GrLo.x, GrLo.y), GrLo.z); float GrOut = min(min(GrHi.x, GrHi.y), GrHi.z); \
	if (GrOut >= max(GrIn, 0.0) && GrIn <= 1.0 && GrIn < GrHitT) { GrHitT = max(GrIn, 0.0); GrHitType = K; } }
$(GAME_BOX_TESTS)
#undef GR_BOX
	}
	if (GrHitType > 0.5)
	{
		float3 GrAt = GrP0 + GrSeg * GrHitT;
		if (GrHitType > 1.5 && User.RainGameScoring > 0.5)
		{
			GrHit = true;
			GrHitPos = GrAt;
		}
		// Absorbed or hit: back by one period, to where this slot re-enters (upwind, hidden), at a new spot.
		PrP = GrC + GrU * ((2.0 * GrR0 - 1.0) * GrH) + GrV * ((2.0 * GrR1 - 1.0) * GrH) + GrD * (dot(GrAt - GrC, GrD) - GrL);
		PrV = GrD * PrVt;
		PrTimer += 1.0;
	}
	else
	{
		GrRel = PrP - GrC;
		GrS = dot(GrRel, GrD);
		GrA = dot(GrRel, GrU);
		GrB = dot(GrRel, GrV);
		if (GrS > 0.5 * GrL)
		{
			// Out through the far face: the slot re-enters one period back, at a new spot.
			PrP = GrC + GrU * ((2.0 * GrR0 - 1.0) * GrH) + GrV * ((2.0 * GrR1 - 1.0) * GrH) + GrD * (GrS - GrL);
			PrTimer += 1.0;
		}
		else if (abs(GrA) > GrH || abs(GrB) > GrH)
		{
			// Blown out of the side: the stream is periodic across the face too.
			GrA -= 2.0 * GrH * round(GrA / (2.0 * GrH));
			GrB -= 2.0 * GrH * round(GrB / (2.0 * GrH));
			PrP = GrC + GrU * GrA + GrV * GrB + GrD * GrS;
		}
	}
}
Particles.RainGameHit = GrHit;
Particles.RainGameHitPos = GrHitPos;

float PrSpeed = length(PrV);
if (PrSpeed > 2.0)
{
	PrDir = PrV / PrSpeed;
}
float PrW = PrDcm * User.RainSizeScale;
// Visible inside the box only, fading in and out over the last $(GAME_FACE_FADE) cm at either face.
float GrSNow = dot(PrP - GrC, GrD);
float PrVis = GrActive ? saturate((GrSNow + 0.5 * GrL) / $(GAME_FACE_FADE)) * saturate((0.5 * GrL - GrSNow) / $(GAME_FACE_FADE)) : 0.0;

