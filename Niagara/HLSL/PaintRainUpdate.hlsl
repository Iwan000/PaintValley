// =====================================================================================
// Project Rain - Paint Valley, System 1 (paint rain): update (custom HLSL module, GPU, Map in / MapOut).
//
// The rain is a fixed pool: no drop is ever spawned after the start, and none dies. Each drop cycles through
// Particles.PaintState:
//   0 cloud     waiting upwind, hidden, for a random time (up to User.RainPaintCloudWait s), then it falls if the
//               pool still has room for it (below);
//   1 falling   steered, collided;
//   2 ripple    landed: a ring on the face (if it is a floor), then back to the cloud.
// Rules while falling:
//   1. Steer: the velocity relaxes towards the current direction D at the drop's speed (time constant
//      $(STEER_TAU) s), so when the player turns the rain, the stream in the air bends like a hose's.
//   2. Collide: the step is tested against the level's boxes (slab test, no tunnelling; the face entered gives
//      the normal). Only the target takes paint (the rest repels it, like oil and water): a drop that lands on a
//      target face that is not yet full (User.RainBox<i>Full: one bit per face) while the round is scored raises
//      Particles.RainGameHit for that frame: Export Particle Data reads back the hit point, the kind
//      (Particles.PaintKind: the box kind, + 10 for a small drop) and the velocity. The game decides from the paint
//      it holds: a big drop seeds a puddle; a small drop on paint feeds that puddle and is used up, one on bare
//      surface is not.
//   3. Recycle: a drop that leaves the stream's bounding sphere goes back to the cloud.
// The pool: the game counts the small drops used up and passes the share left (User.RainPaintLeft). A drop leaves
// the cloud only while its rank (Particles.RainRand.w) is within that share: small drops by their rank among the
// small ones, big drops (Particles.PaintCarrier, User.RainPaintBigFrac of the pool) among the big ones, so the big
// drops stay a fixed share of the rain. Which drop stays in the cloud does not show: the rain thins as it is used.
// User.RainFreeze (0..1) slows everything to a stop: the drops hang in the air and round up. A new
// User.RainPaintEpoch brings every drop back to the cloud (a new round) without spawning any.
// $(...) placeholders are replaced by the C++ builder.
// =====================================================================================
#define PN_NEXT(S) S = S * 747796405u + 2891336453u;
#define PN_U01(S) (float(((((S) >> (((S) >> 28u) + 4u)) ^ (S)) * 277803737u) >> 8u) * (1.0 / 16777216.0))
float PnDt = clamp(Engine.DeltaTime, 1e-5, 0.05) * (1.0 - saturate(User.RainFreeze));
float3 PnP = Particles.Position;
float3 PnVel = Particles.Velocity;
float4 PnRnd = Particles.RainRand;
float PnTimer = Particles.RainTimer;
float PnAge = Particles.PaintAge;
float PnClock = Particles.PaintRipple;
float PnState = Particles.PaintState;
float3 PnN = Particles.PaintNormal;
bool PnBig = Particles.PaintCarrier > 0.5;

float3 PnD = normalize(User.RainGameDir);
float3 PnU = normalize(cross(PnD, abs(PnD.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 PnV = cross(PnD, PnU);
float3 PnC = User.RainGameCenter;
float PnH = User.RainGameHalf;
float PnL = User.RainGameLength;
bool PnOn = User.RainGameActive > 0.5;
float PnSpeed = User.RainPaintSpeed * (0.9 + 0.2 * PnRnd.x);
float PnExit = sqrt(0.25 * PnL * PnL + PnH * PnH);

// Fresh randoms: the drop's own seed and how often it has landed or restarted.
uint PnS = uint(PnRnd.y * 16777215.0) * 2654435761u + uint(PnTimer) * 747796405u + $(SEED)u;
PN_NEXT(PnS) float PnA0 = PN_U01(PnS);
PN_NEXT(PnS) float PnA1 = PN_U01(PnS);
PN_NEXT(PnS) float PnA2 = PN_U01(PnS);
float PnRad = PnH * sqrt(PnA0);
float PnAng = 6.2831853 * PnA1;
float3 PnRestart = PnC + (PnU * cos(PnAng) + PnV * sin(PnAng)) * PnRad - PnD * (0.5 * PnL);

// A new round: every drop back to the cloud, waiting a random share of the cloud time.
if (abs(Particles.PaintEpoch - User.RainPaintEpoch) > 0.5)
{
	Particles.PaintEpoch = User.RainPaintEpoch;
	PnState = 0.0;
	PnClock = PnA2 * User.RainPaintCloudWait;
	PnTimer += 1.0;
}
// A drop takes part while its rank is within the share of the pool left.
float PnBigFrac = clamp(User.RainPaintBigFrac, 0.0, 0.999);
float PnRank = PnBig ? PnRnd.w / max(PnBigFrac, 1e-6) : (PnRnd.w - PnBigFrac) / (1.0 - PnBigFrac);
bool PnActive = PnRank < User.RainPaintLeft;

bool PnHit = false;
float3 PnHitPos = Particles.RainGameHitPos;
float PnKind = 0.0;
if (PnOn)
{
	if (PnState < 0.5)
	{
		// ---- the cloud: wait, then fall from a fresh spot upwind.
		PnClock -= PnDt;
		if (PnClock <= 0.0)
		{
			if (PnActive)
			{
				PnState = 1.0;
				PnP = PnRestart;
				PnVel = PnD * PnSpeed;
				PnAge = 0.0;
			}
			else
			{
				PnClock = PnA2 * User.RainPaintCloudWait;
				PnTimer += 1.0;
			}
		}
	}
	else if (PnState < 1.5)
	{
		// ---- rule 1: steer towards D.
		PnVel += (PnD * PnSpeed - PnVel) * (1.0 - exp(-PnDt / $(STEER_TAU)));
		float3 PnP0 = PnP;
		float3 PnSeg = PnVel * PnDt;
		PnP = PnP0 + PnSeg;

		// ---- rule 2: the first box this step crosses, the face it enters and that box's full faces.
		float PnHitT = 2.0;
		float PnHitK = 0.0;
		float PnHitF = 0.0;
		float3 PnHitN = float3(0.0, 0.0, 1.0);
		float3 PnInv = 1.0 / (PnSeg + 1e-4 * (1.0 - abs(sign(PnSeg))));
#define GR_BOX(Mn, Mx, K, F) if (K > 0.5) { float3 PnT1 = (Mn - PnP0) * PnInv; float3 PnT2 = (Mx - PnP0) * PnInv; \
	float3 PnLo = min(PnT1, PnT2); float3 PnHi = max(PnT1, PnT2); \
	float PnIn = max(max(PnLo.x, PnLo.y), PnLo.z); float PnOut = min(min(PnHi.x, PnHi.y), PnHi.z); \
	if (PnOut >= max(PnIn, 0.0) && PnIn <= 1.0 && PnIn < PnHitT) { PnHitT = max(PnIn, 0.0); PnHitK = K; PnHitF = F; \
		PnHitN = PnLo.x >= PnLo.y && PnLo.x >= PnLo.z ? float3(-sign(PnSeg.x), 0.0, 0.0) \
			: (PnLo.y >= PnLo.z ? float3(0.0, -sign(PnSeg.y), 0.0) : float3(0.0, 0.0, -sign(PnSeg.z))); } }
$(PAINT_BOX_TESTS)
#undef GR_BOX

		bool PnLeft = length(PnP - PnC) > PnExit && dot(PnVel, PnP - PnC) > 0.0;
		if (PnHitK > 0.5)
		{
			// The face's bit: +X 1, -X 2, +Y 4, -Y 8, +Z 16, -Z 32.
			float PnBit = PnHitN.x > 0.5 ? 1.0 : PnHitN.x < -0.5 ? 2.0 : PnHitN.y > 0.5 ? 4.0 : PnHitN.y < -0.5 ? 8.0 : PnHitN.z > 0.5 ? 16.0 : 32.0;
			bool PnFull = fmod(floor(PnHitF / PnBit), 2.0) > 0.5;
			bool PnTakes = User.RainGameScoring > 0.5 && abs(PnHitK - $(KIND_TARGET)) < 0.5 && !PnFull;
			float3 PnAt = PnP0 + PnSeg * PnHitT;
			if (PnTakes)
			{
				PnHit = true;
				PnHitPos = PnAt;
				PnKind = PnHitK + (PnBig ? 0.0 : 10.0);
			}
			// The ring, just off the face it hit; then the cloud.
			PnP = PnAt + PnHitN * 0.4;
			PnN = PnHitN;
			PnState = 2.0;
			PnClock = $(RIPPLE_LIFE);
			PnTimer += 1.0;
		}
		else if (PnLeft)
		{
			PnState = 0.0;
			PnClock = PnA2 * User.RainPaintCloudWait;
			PnTimer += 1.0;
		}
	}
	else
	{
		// ---- the ring fades; then the cloud.
		PnClock -= PnDt;
		if (PnClock <= 0.0)
		{
			PnState = 0.0;
			PnClock = PnA2 * User.RainPaintCloudWait;
			PnTimer += 1.0;
		}
	}
	PnAge += PnDt;
}
bool PnRing = PnState > 1.5;
Particles.Position = PnP;
Particles.Velocity = PnState > 1.5 ? float3(0.0, 0.0, 0.0) : PnVel;
Particles.RainTimer = PnTimer;
Particles.PaintAge = PnAge;
Particles.PaintRipple = PnClock;
Particles.PaintState = PnState;
Particles.PaintNormal = PnN;
Particles.RainGameHit = PnHit;
Particles.RainGameHitPos = PnHitPos;
Particles.PaintKind = PnKind;

// ---- the look. Falling: a capsule along the velocity, facing the camera (User.RainViewFacing). Held (the freeze):
// the streak draws in to a round drop, $(BEAD_SCALE) times as wide. User.RainPaintFilm picks the material's look for
// the drops in the air (paint, or the film's water drops: nearly opaque, the image they show is what lies behind).
// Landed: a ring lying on the face, spreading and fading, on floors only. Waiting: nothing.
float PnSp = length(PnVel);
float PnHold = saturate(User.RainFreeze);
bool PnFilm = User.RainPaintFilm > 0.5;
float3 PnRel = PnP - PnC;
float PnFadeOut = dot(PnVel, PnRel) > 0.0 ? saturate((PnExit - length(PnRel)) / 60.0) : 1.0;
float PnAcross = length(PnRel - PnD * dot(PnRel, PnD));
float PnSide = saturate((PnH - PnAcross) / (0.3 * PnH));
float PnW = PnBig ? $(BIG_WIDTH) * (0.85 + 0.3 * PnRnd.z) : $(SMALL_WIDTH) * (0.8 + 0.4 * PnRnd.z);
if (PnRing)
{
	float PnRa = 1.0 - PnClock / $(RIPPLE_LIFE);
	float PnRs = (PnBig ? $(RIPPLE_SIZE_BIG) : $(RIPPLE_SIZE)) * (0.45 + 0.55 * sqrt(PnRa));
	float PnUp = step(0.5, PnN.z);
	Particles.SpriteSize = PnOn && PnUp > 0.5 ? float2(PnRs, PnRs) : float2(0.0, 0.0);
	Particles.SpriteFacing = PnN;
	Particles.SpriteAlignment = normalize(cross(PnN, abs(PnN.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
	Particles.Color = float4(User.RainPaintInk.rgb, $(RIPPLE_ALPHA) * saturate(PnRa * 6.0) * (1.0 - PnRa) * PnUp * (PnOn ? 1.0 : 0.0));
	Particles.DynamicMaterialParameter = float4(2.0, 1.0, PnRa, 0.0);
}
else
{
	float PnVis = PnOn && PnState > 0.5 && PnState < 1.5 ? saturate(PnAge / 0.12) * PnFadeOut * PnSide : 0.0;
	float PnBeadW = PnW * lerp(1.0, $(BEAD_SCALE), PnHold);
	float PnLen = PnBeadW + PnSp * (1.0 - PnHold) * (PnBig ? $(BIG_STREAK) : $(SMALL_STREAK));
	float PnAlpha = PnFilm ? (PnBig ? 0.95 : 0.85) : (PnBig ? lerp(0.95, $(BEAD_ALPHA_BIG), PnHold) : lerp($(SMALL_ALPHA), $(BEAD_ALPHA), PnHold));
	// The player's opacity (the panel's slider), on top.
	PnAlpha = saturate(PnAlpha * User.RainPaintOpacity);
	Particles.SpriteSize = float2(PnBeadW, PnLen) * (PnVis > 0.001 ? 1.0 : 0.0);
	Particles.SpriteFacing = User.RainViewFacing;
	Particles.SpriteAlignment = PnSp > 1.0 ? PnVel / PnSp : PnD;
	Particles.Color = float4(User.RainPaintInk.rgb, PnAlpha * PnVis);
	Particles.DynamicMaterialParameter = float4(PnBig ? 1.0 : 0.0, PnBeadW / PnLen, PnHold, PnFilm ? 1.0 : 0.0);
}
#undef PN_NEXT
#undef PN_U01
